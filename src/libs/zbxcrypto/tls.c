/*
** Zabbix
** Copyright (C) 2001-2015 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"
#include "comms.h"
#include "log.h"
#include "tls.h"
#include "tls_tcp.h"
#include "tls_tcp_active.h"
#include "db.h"

#if defined(HAVE_POLARSSL)
#	include <polarssl/entropy.h>
#	include <polarssl/ctr_drbg.h>
#	include <polarssl/ssl.h>
#	include <polarssl/error.h>
#	include <polarssl/debug.h>
#elif defined(HAVE_GNUTLS)
#	include <gnutls/gnutls.h>
#elif defined(HAVE_OPENSSL)
#	include <openssl/ssl.h>
#endif

/* Currently use only TLS 1.2, which has number 3.3. In 2015 a new standard for TLS 1.3 is expected. */
/* Then we might need to support both TLS 1.2 and 1.3 to work with older Zabbix agents. */
#if defined(HAVE_POLARSSL)
#	define ZBX_TLS_MIN_MAJOR_VER	SSL_MAJOR_VERSION_3
#	define ZBX_TLS_MIN_MINOR_VER	SSL_MINOR_VERSION_3
#	define ZBX_TLS_MAX_MAJOR_VER	SSL_MAJOR_VERSION_3
#	define ZBX_TLS_MAX_MINOR_VER	SSL_MINOR_VERSION_3
#	define ZBX_TLS_CIPHERSUITE_CERT	0			/* select only certificate ciphersuites */
#	define ZBX_TLS_CIPHERSUITE_PSK	1			/* select only pre-shared key ciphersuites */
#	define ZBX_TLS_CIPHERSUITE_ALL	2			/* select ciphersuites with certificate and PSK */
#endif

extern unsigned int	configured_tls_connect_mode;
extern unsigned int	configured_tls_accept_modes;

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
extern unsigned char	process_type, program_type;
extern char		*CONFIG_TLS_CONNECT;
extern char		*CONFIG_TLS_ACCEPT;
extern char		*CONFIG_TLS_CA_FILE;
extern char		*CONFIG_TLS_CA_PATH;
extern char		*CONFIG_TLS_CRL_FILE;
extern char		*CONFIG_TLS_CERT_FILE;
extern char		*CONFIG_TLS_KEY_FILE;
extern char		*CONFIG_TLS_PSK_FILE;
extern char		*CONFIG_TLS_PSK_IDENTITY;
#endif

#if defined(HAVE_POLARSSL)
static x509_crt		*ca_cert		= NULL;
static x509_crl		*crl			= NULL;
static x509_crt		*my_cert		= NULL;
static pk_context	*my_priv_key		= NULL;
static char		*my_psk			= NULL;
static size_t		my_psk_len		= 0;
static char		*my_psk_identity	= NULL;
static size_t		my_psk_identity_len	= 0;
static entropy_context	*entropy		= NULL;
static ctr_drbg_context	*ctr_drbg		= NULL;
static char		*err_msg		= NULL;
static int		*ciphersuites_cert	= NULL;
static int		*ciphersuites_psk	= NULL;
static int		*ciphersuites_all	= NULL;
static char		work_buf[SSL_MAX_CONTENT_LEN + 1];
/* Pointer to DCget_psk_by_identity() initialized at runtime. This is a workaround for linking. */
/* Server and proxy link with src/libs/zbxdbcache/dbconfig.o where DCget_psk_by_identity() resides */
/* but other components (e.g. agent) do not link dbconfig.o. */
size_t			(*find_psk_in_cache)(const unsigned char *, unsigned char *, size_t) = NULL;
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: zbx_tls_make_personalization_string                              *
 *                                                                            *
 * Purpose: provide additional entropy for initialization of crypto library   *
 *                                                                            *
 * Comments:                                                                  *
 *     For more information about why and how to use personalization strings  *
 *     see                                                                    *
 *     https://polarssl.org/module-level-design-rng                           *
 *     http://csrc.nist.gov/publications/nistpubs/800-90A/SP800-90A.pdf       *
 *                                                                            *
 ******************************************************************************/
static void	zbx_tls_make_personalization_string(char **pers, size_t *len)
{
	/* TODO: follow recommendations in http://csrc.nist.gov/publications/nistpubs/800-90A/SP800-90A.pdf */
	/* and add more entropy into the personalization string (e.g. process PID, microseconds of current time etc.) */
	/* Pay attention to the personalization string length as described in SP800-90A.pdf */

	/* For demo purposes only. TODO: replace with production code. */
#define DEMO_PERS_STRING	"Zabbix TLSZabbix TLSZabbix TLSZabbix TLSZabbix TLS"
	*pers = zbx_strdup(*pers, DEMO_PERS_STRING);
	*len = strlen(DEMO_PERS_STRING);
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: polarssl_debug                                                   *
 *                                                                            *
 * Purpose: write a PolarSSL debug message into Zabbix log                    *
 *                                                                            *
 * Comments:                                                                  *
 *     Parameter 'tls_ctx' is not used but cannot be removed because this is  *
 *     a callback function, its arguments are defined in PolarSSL.            *
 ******************************************************************************/
static void	polarssl_debug(void *tls_ctx, int level, const char *str)
{
	char	msg[1024];	/* Apparently 1024 bytes is the longest message which can come from PolarSSL 1.3.9 */

	/* TODO code can be improved by allocating a dynamic buffer if message is longer than 1024 bytes */
	/* remove '\n' from the end of debug message */
	zbx_strlcpy(msg, str, sizeof(msg));
	zbx_rtrim(msg, "\n");
	zabbix_log(LOG_LEVEL_DEBUG, "PolarSSL debug: level=%d \"%s\"", level, msg);
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: zbx_tls_error_msg                                                *
 *                                                                            *
 * Purpose: compose a TLS error message                                       *
 *                                                                            *
 ******************************************************************************/
static void	zbx_tls_error_msg(int error_code, const char *msg, char **error)
{
	char	err[128];	/* 128 bytes are enough for PolarSSL error messages */

	polarssl_strerror(error_code, err, sizeof(err));
	*error = zbx_dsprintf(*error, "%s%s", msg, err);
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: zbx_tls_validate_config                                          *
 *                                                                            *
 * Purpose: check for allowed combinations of TLS configuration parameters    *
 *                                                                            *
 ******************************************************************************/
static void	zbx_tls_validate_config(void)
{
	/* either both a certificate and a private key must be defined or none of them */

	if (NULL != my_cert && NULL == my_priv_key)
	{
		zabbix_log(LOG_LEVEL_CRIT, "configuration parameter \"TLSCertFile\" is defined but \"TLSKeyFile\" is"
				" not defined");
		goto out;
	}

	if (NULL == my_cert && NULL != my_priv_key)
	{
		zabbix_log(LOG_LEVEL_CRIT, "configuration parameter \"TLSKeyFile\" is defined but \"TLSCertFile\" is"
				" not defined");
		goto out;
	}

	/* CA file or directory must be defined only together with a certificate */

	if (NULL != my_cert && NULL == ca_cert)
	{
		zabbix_log(LOG_LEVEL_CRIT, "configuration parameter \"TLSCertFile\" is defined but neither \"TLSCaFile\""
				" nor \"TLSCaPath\" is defined");
		goto out;
	}

	if (NULL == my_cert && NULL != ca_cert)
	{
		zabbix_log(LOG_LEVEL_CRIT, "configuration parameter \"TLSCaFile\" or \"TLSCaPath\" is defined but "
				"\"TLSCertFile\" and \"TLSKeyFile\" are not defined");
		goto out;
	}

	/* CRL file must be defined only together with a certificate */

	if (NULL == my_cert && NULL != crl)
	{
		zabbix_log(LOG_LEVEL_CRIT, "configuration parameter \"TLSCrlFile\" is defined but \"TLSCertFile\" and "
				"\"TLSKeyFile\" are not defined");
		goto out;
	}

	/* either both a PSK and a PSK-identity must be defined or none of them */

	if (NULL != my_psk && NULL == my_psk_identity)
	{
		zabbix_log(LOG_LEVEL_CRIT, "configuration parameter \"TLSPskFile\" is defined but \"TLSPskIdentity\" is"
				" not defined");
		goto out;
	}

	if (NULL == my_psk && NULL != my_psk_identity)
	{
		zabbix_log(LOG_LEVEL_CRIT, "configuration parameter \"TLSPskIdentity\" is defined but \"TLSPskFile\" is"
				" not defined");
		goto out;
	}

	/* agentd and active proxy specific validation */

	if (0 != (program_type & ZBX_PROGRAM_TYPE_AGENTD) || 0 != (program_type & ZBX_PROGRAM_TYPE_PROXY_ACTIVE))
	{
		/* 'TLSConnect' is the master parameter to be matched by certificate and PSK parameters. */
		/* 'TLSConnect' will be silently ignored on agentd, if active checks are not configured */
		/* (i.e. 'ServerActive' is not specified. */

		if ((NULL != my_cert || NULL != my_psk) && (NULL == CONFIG_TLS_CONNECT ||
				(NULL != CONFIG_TLS_CONNECT && '\0' == *CONFIG_TLS_CONNECT)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "certificate or pre-shared key (PSK) is configured but parameter "
					"\"TLSConnect\" is not defined");
			goto out;
		}

		if (0 != (configured_tls_connect_mode & ZBX_TCP_SEC_TLS_CERT) && NULL == my_cert)
		{
			zabbix_log(LOG_LEVEL_CRIT, "parameter \"TLSConnect\" value requires a certificate but it is not"
					" configured");
			goto out;
		}

		if (0 != (configured_tls_connect_mode & ZBX_TCP_SEC_TLS_PSK) && NULL == my_psk)
		{
			zabbix_log(LOG_LEVEL_CRIT, "parameter \"TLSConnect\" value requires a pre-shared key (PSK) but "
					"it is not configured");
			goto out;
		}
	}

	/* agentd, agent and passive proxy specific validation */

	if (0 != (program_type & ZBX_PROGRAM_TYPE_AGENTD) || 0 != (program_type & ZBX_PROGRAM_TYPE_PROXY_PASSIVE) ||
			0 != (program_type & ZBX_PROGRAM_TYPE_AGENT))
	{
		/* 'TLSAccept' is the master parameter to be matched by certificate and PSK parameters */

		if ((NULL != my_cert || NULL != my_psk) && (NULL == CONFIG_TLS_ACCEPT ||
				(NULL != CONFIG_TLS_ACCEPT && '\0' == *CONFIG_TLS_ACCEPT)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "certificate or pre-shared key (PSK) is configured but parameter "
					"\"TLSAccept\" is not defined");
			goto out;
		}

		if (0 != (configured_tls_accept_modes & ZBX_TCP_SEC_TLS_CERT) && NULL == my_cert)
		{
			zabbix_log(LOG_LEVEL_CRIT, "parameter \"TLSAccept\" value requires a certificate but it is not"
					" configured");
			goto out;
		}

		if (0 != (configured_tls_accept_modes & ZBX_TCP_SEC_TLS_PSK) && NULL == my_psk)
		{
			zabbix_log(LOG_LEVEL_CRIT, "parameter \"TLSAccept\" value requires a pre-shared key (PSK) but "
					"it is not configured");
			goto out;
		}
	}

	return;
out:
	zbx_tls_free();
	exit(EXIT_FAILURE);
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: zbx_is_ciphersuite_cert                                          *
 *                                                                            *
 * Purpose: does the specified ciphersuite ID refer to a non-PSK              *
 *          (i.e. certificate) ciphersuite supported for the specified TLS    *
 *          version range                                                     *
 *                                                                            *
 * Comments:                                                                  *
 *          RFC 7465 "Prohibiting RC4 Cipher Suites" requires that RC4 should *
 *          never be used. Also, discard weak encryptions.                    *
 *                                                                            *
 ******************************************************************************/
static int	zbx_is_ciphersuite_cert(const int *p)
{
	const ssl_ciphersuite_t	*info;

	/* PolarSSL function ssl_ciphersuite_uses_psk() is not used here because it can be unavailable in some */
	/* installations. */
	if (NULL != (info = ssl_ciphersuite_from_id(*p)) && POLARSSL_KEY_EXCHANGE_PSK != info->key_exchange &&
			POLARSSL_KEY_EXCHANGE_DHE_PSK != info->key_exchange &&
			POLARSSL_KEY_EXCHANGE_ECDHE_PSK != info->key_exchange &&
			POLARSSL_KEY_EXCHANGE_RSA_PSK != info->key_exchange &&
			POLARSSL_CIPHER_ARC4_128 != info->cipher && 0 == (info->flags & POLARSSL_CIPHERSUITE_WEAK) &&
			(ZBX_TLS_MIN_MAJOR_VER > info->min_major_ver || (ZBX_TLS_MIN_MAJOR_VER == info->min_major_ver &&
			ZBX_TLS_MIN_MINOR_VER >= info->min_minor_ver)) &&
			(ZBX_TLS_MAX_MAJOR_VER < info->max_major_ver || (ZBX_TLS_MAX_MAJOR_VER == info->max_major_ver &&
			ZBX_TLS_MAX_MINOR_VER <= info->max_minor_ver)))
	{
		return SUCCEED;
	}
	else
		return FAIL;
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: zbx_is_ciphersuite_psk                                           *
 *                                                                            *
 * Purpose: does the specified ciphersuite ID refer to a PSK ciphersuite      *
 *          supported for the specified TLS version range                     *
 *                                                                            *
 * Comments:                                                                  *
 *          RFC 7465 "Prohibiting RC4 Cipher Suites" requires that RC4 should *
 *          never be used. Also, discard weak encryptions.                    *
 *                                                                            *
 ******************************************************************************/
static int	zbx_is_ciphersuite_psk(const int *p)
{
	const ssl_ciphersuite_t	*info;

	if (NULL != (info = ssl_ciphersuite_from_id(*p)) && (POLARSSL_KEY_EXCHANGE_PSK == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_DHE_PSK == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_ECDHE_PSK == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_RSA_PSK == info->key_exchange) &&
			POLARSSL_CIPHER_ARC4_128 != info->cipher && 0 == (info->flags & POLARSSL_CIPHERSUITE_WEAK) &&
			(ZBX_TLS_MIN_MAJOR_VER > info->min_major_ver || (ZBX_TLS_MIN_MAJOR_VER == info->min_major_ver &&
			ZBX_TLS_MIN_MINOR_VER >= info->min_minor_ver)) &&
			(ZBX_TLS_MAX_MAJOR_VER < info->max_major_ver || (ZBX_TLS_MAX_MAJOR_VER == info->max_major_ver &&
			ZBX_TLS_MAX_MINOR_VER <= info->max_minor_ver)))
	{
		return SUCCEED;
	}
	else
		return FAIL;
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: zbx_is_ciphersuite_all                                           *
 *                                                                            *
 * Purpose: does the specified ciphersuite ID refer to a good ciphersuite     *
 *          supported for the specified TLS version range                     *
 *                                                                            *
 * Comments:                                                                  *
 *          RFC 7465 "Prohibiting RC4 Cipher Suites" requires that RC4 should *
 *          never be used. Also, discard weak encryptions.                    *
 *                                                                            *
 ******************************************************************************/
static int	zbx_is_ciphersuite_all(const int *p)
{
	const ssl_ciphersuite_t	*info;

	if (NULL != (info = ssl_ciphersuite_from_id(*p)) &&
			POLARSSL_CIPHER_ARC4_128 != info->cipher && 0 == (info->flags & POLARSSL_CIPHERSUITE_WEAK) &&
			(ZBX_TLS_MIN_MAJOR_VER > info->min_major_ver || (ZBX_TLS_MIN_MAJOR_VER == info->min_major_ver &&
			ZBX_TLS_MIN_MINOR_VER >= info->min_minor_ver)) &&
			(ZBX_TLS_MAX_MAJOR_VER < info->max_major_ver || (ZBX_TLS_MAX_MAJOR_VER == info->max_major_ver &&
			ZBX_TLS_MAX_MINOR_VER <= info->max_minor_ver)))
	{
		return SUCCEED;
	}
	else
		return FAIL;
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: zbx_ciphersuites                                                 *
 *                                                                            *
 * Purpose: copy a list of ciphersuites (certificate- or PSK-related) from a  *
 *          list of all supported ciphersuites                                *
 *                                                                            *
 ******************************************************************************/
static unsigned int	zbx_ciphersuites(int type, int **suites)
{
	const int		*supported_suites, *p;
	int			*q;
	unsigned int		count = 0;

	supported_suites = ssl_list_ciphersuites();

	/* count available relevant ciphersuites */
	for (p = supported_suites; *p != 0; p++)
	{
		if (ZBX_TLS_CIPHERSUITE_CERT == type)
		{
			if (SUCCEED != zbx_is_ciphersuite_cert(p))
				continue;
		}
		else if (ZBX_TLS_CIPHERSUITE_PSK == type)
		{
			if (SUCCEED != zbx_is_ciphersuite_psk(p))
				continue;
		}
		else	/* ZBX_TLS_CIPHERSUITE_ALL */
		{
			if (SUCCEED != zbx_is_ciphersuite_all(p))
				continue;
		}

		count++;
	}

	*suites = zbx_malloc(*suites, (count + 1) * sizeof(int));

	/* copy the ciphersuites into array */
	for (p = supported_suites, q = *suites; *p != 0; p++)
	{
		if (ZBX_TLS_CIPHERSUITE_CERT == type)
		{
			if (SUCCEED != zbx_is_ciphersuite_cert(p))
				continue;
		}
		else if (ZBX_TLS_CIPHERSUITE_PSK == type)
		{
			if (SUCCEED != zbx_is_ciphersuite_psk(p))
				continue;
		}
		else	/* ZBX_TLS_CIPHERSUITE_ALL */
		{
			if (SUCCEED != zbx_is_ciphersuite_all(p))
				continue;
		}

		*q++ = *p;
	}

	*q = 0;

	return count;
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: zbx_psk_hex2bin                                                  *
 *                                                                            *
 * Purpose:                                                                   *
 *    convert a pre-shared key from a textual representation (ASCII hex digit *
 *    string) to a binary representation (byte string)                        *
 *                                                                            *
 * Parameters:                                                                *
 *     p_hex   - [IN] null-terminated input PSK hex-string                    *
 *     buf     - [OUT] output buffer                                          *
 *     buf_len - [IN] output buffer size                                      *
 *                                                                            *
 * Return value:                                                              *
 *     Number of PSK bytes written into ' buf' on successful conversion.      *
 *     -1 - an error occurred.                                                *
 *                                                                            *
 * Comments:                                                                  *
 *     In case of error incomplete useless data may be written into 'buf'.    *
 *                                                                            *
 ******************************************************************************/
static int	zbx_psk_hex2bin(const unsigned char *p_hex, unsigned char *buf, int buf_len)
{
	unsigned char		*q, hi, lo;
	int			len = 0;

	q = buf;

	while ('\0' != *p_hex)
	{
		if (0 != isxdigit(*p_hex) && 0 != isxdigit(*(p_hex + 1)) && buf_len > len)
		{
			hi = *p_hex & 0x0f;

			if ('9' < *p_hex++)
				hi += 9u;

			lo = *p_hex & 0x0f;

			if ('9' < *p_hex++)
				lo += 9u;

			*q++ = hi << 4 | lo;
			len++;
		}
		else
			return -1;
	}

	return len;
}
#endif

#if defined(HAVE_POLARSSL)
/******************************************************************************
 *                                                                            *
 * Function: zbx_psk_callback                                                 *
 *                                                                            *
 * Purpose:                                                                   *
 *    find and set the requested pre-shared key upon PolarSSL request         *
 *                                                                            *
 * Parameters:                                                                *
 *     par              - [IN] not used                                       *
 *     tls_ctx          - [IN] TLS connection context                         *
 *     psk_identity     - [IN] PSK identity for which the PSK should be       *
 *                             searched and set                               *
 *     psk_identity_len - [IN] size of 'psk_identity'                         *
 *                                                                            *
 * Return value:                                                              *
 *      0  - required PSK successfully found and set                          *
 *     -1 - an error occurred.                                                *
 *                                                                            *
 * Comments:                                                                  *
 *     A callback function, its arguments are defined in PolarSSL.            *
 *                                                                            *
 ******************************************************************************/
static int	zbx_psk_callback(void *par, ssl_context *tls_ctx, const unsigned char *psk_identity,
		size_t psk_identity_len)
{
	const char	*__function_name = "zbx_psk_callback";
	unsigned char	*psk;
	size_t		psk_len = 0;
	int		psk_bin_len;
	unsigned char	tls_psk_identity[HOST_TLS_PSK_IDENTITY_LEN_MAX], tls_psk_hex[HOST_TLS_PSK_LEN_MAX],
			psk_buf[HOST_TLS_PSK_LEN/2];

	if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_DEBUG))
	{
		/* special print: psk_identity is not '\0'-terminated */
		zabbix_log(LOG_LEVEL_DEBUG, "%s(): requested PSK-identity: \"%.*s\"", __function_name, psk_identity_len,
				psk_identity);
	}

	/* try PSK from configuration file first (it is already in binary form) */

	if (0 < my_psk_identity_len && my_psk_identity_len == psk_identity_len &&
			0 == memcmp(my_psk_identity, psk_identity, psk_identity_len))
	{
		psk = (unsigned char *)my_psk;
		psk_len = my_psk_len;
	}
	else	/* search the required PSK in configuration cache */
	{
		if (HOST_TLS_PSK_IDENTITY_LEN < psk_identity_len)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			return -1;
		}

		memcpy(tls_psk_identity, psk_identity, psk_identity_len);
		tls_psk_identity[psk_identity_len] = '\0';

		/* call the function DCget_psk_by_identity() by pointer */
		if (0 < find_psk_in_cache(tls_psk_identity, tls_psk_hex, HOST_TLS_PSK_LEN_MAX))
		{
			/* convert PSK to binary form */
			if (0 >= (psk_bin_len = zbx_psk_hex2bin(tls_psk_hex, psk_buf, sizeof(psk_buf))))
			{
				/* this should have been prevented by validation in frontend or API */
				zabbix_log(LOG_LEVEL_WARNING, "cannot convert PSK to binary form for PSK identity "
						"\"%.*s\"", psk_identity_len, psk_identity);
				return -1;
			}

			psk = psk_buf;
			psk_len = (size_t)psk_bin_len;
		}
		else
		{
			if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_DEBUG))
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot find requested PSK-identity: \"%.*s\"",
						__function_name, psk_identity_len, psk_identity);
			}
		}
	}

	if (0 < psk_len)
	{
		int 	res;

		if (0 == (res = ssl_set_psk(tls_ctx, psk, psk_len, psk_identity, psk_identity_len)))
		{
			return 0;
		}
		else
		{
			zbx_tls_error_msg(res, "", &err_msg);
			zabbix_log(LOG_LEVEL_WARNING, "cannot set PSK for PSK identity \"%.*s\": %s", psk_identity_len,
					psk_identity, err_msg);
			zbx_free(err_msg);
		}
	}

	return -1;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_tls_init_parent                                              *
 *                                                                            *
 * Purpose: initialize TLS library in a parent process                        *
 *                                                                            *
 ******************************************************************************/
int	zbx_tls_init_parent(void)
{
	const char	*__function_name = "zbx_tls_init_parent";
	int		ret = SUCCEED;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);
	/* TODO fill in implementation */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tls_init_child                                               *
 *                                                                            *
 * Purpose: read available configuration parameters and initialize TLS        *
 *          library in a child process                                        *
 *                                                                            *
 ******************************************************************************/
void	zbx_tls_init_child(void)
{
	const char	*__function_name = "zbx_tls_init_child";

#if defined(HAVE_POLARSSL)
	int		res;
	unsigned int	cipher_count;
	char		*pers = NULL;
	size_t		pers_len = 0;
#endif

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);
#if defined(HAVE_POLARSSL)
	/* parse 'TLSConnect' configuration parameter (in zabbix_proxy.conf, zabbix_agentd.conf) */
	if (NULL != CONFIG_TLS_CONNECT && '\0' != *CONFIG_TLS_CONNECT)
	{
		if (0 == strcmp(CONFIG_TLS_CONNECT, ZBX_TCP_SEC_UNENCRYPTED_TXT))
			configured_tls_connect_mode = ZBX_TCP_SEC_UNENCRYPTED;
		else if (0 == strcmp(CONFIG_TLS_CONNECT, ZBX_TCP_SEC_TLS_CERT_TXT))
			configured_tls_connect_mode = ZBX_TCP_SEC_TLS_CERT;
		else if (0 == strcmp(CONFIG_TLS_CONNECT, ZBX_TCP_SEC_TLS_PSK_TXT))
			configured_tls_connect_mode = ZBX_TCP_SEC_TLS_PSK;
		else
		{
			zabbix_log(LOG_LEVEL_CRIT, "invalid value of \"TLSConnect\" parameter");
			zbx_tls_free();
			exit(EXIT_FAILURE);
		}
	}

	/* parse 'TLSAccept' configuration parameter (in zabbix_proxy.conf, zabbix_agentd.conf, zabbix_agent.conf) */
	if (NULL != CONFIG_TLS_ACCEPT && '\0' != *CONFIG_TLS_ACCEPT)
	{
		char	*s, *p, *delim;

		configured_tls_accept_modes = 0;
		p = s = zbx_strdup(NULL, CONFIG_TLS_ACCEPT);

		while (1)
		{
			delim = (NULL == p ? NULL : strchr(p, ','));
			if (NULL != delim)
				*delim = '\0';

			if (0 == strcmp(p, ZBX_TCP_SEC_UNENCRYPTED_TXT))
				configured_tls_accept_modes |= ZBX_TCP_SEC_UNENCRYPTED;
			else if (0 == strcmp(p, ZBX_TCP_SEC_TLS_CERT_TXT))
				configured_tls_accept_modes |= ZBX_TCP_SEC_TLS_CERT;
			else if (0 == strcmp(p, ZBX_TCP_SEC_TLS_PSK_TXT))
				configured_tls_accept_modes |= ZBX_TCP_SEC_TLS_PSK;
			else
			{
				zabbix_log(LOG_LEVEL_CRIT, "invalid value of \"TLSAccept\" parameter");
				zbx_free(s);
				zbx_tls_free();
				exit(EXIT_FAILURE);
			}

			if (NULL == p || NULL == delim)
				break;

			*delim = ',';
			p = delim + 1;
		}

		zbx_free(s);
	}

	/* 'TLSCaPath' parameter (in zabbix_server.conf, zabbix_proxy.conf, zabbix_agentd.conf, zabbix_agent.conf). */
	/* Try to configure the CA path first, it overrides the CA file parameter. */
	if (NULL != CONFIG_TLS_CA_PATH && '\0' != *CONFIG_TLS_CA_PATH)
	{
		ca_cert = zbx_malloc(ca_cert, sizeof(x509_crt));
		x509_crt_init(ca_cert);

		if (0 != (res = x509_crt_parse_path(ca_cert, CONFIG_TLS_CA_PATH)))
		{
			if (0 > res)
			{
				zbx_tls_error_msg(res, "", &err_msg);
				zabbix_log(LOG_LEVEL_CRIT, "cannot parse CA certificate(s) in directory \"%s\": %s",
						CONFIG_TLS_CA_PATH, err_msg);
				zbx_free(err_msg);
			}
			else
			{
				zabbix_log(LOG_LEVEL_CRIT, "cannot parse %d CA certificate(s) in directory \"%s\"", res,
						CONFIG_TLS_CA_PATH);
			}

			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_DEBUG))
		{
			/* x509_crt_info() uses a fixed buffer and there is no way to know how large this buffer */
			/* should be to accumulate all info. */
			if (-1 != x509_crt_info(work_buf, sizeof(work_buf), "", ca_cert))
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s(): successfully loaded CA certificate(s) from directory"
						" (output may be truncated):\n%s", __function_name, work_buf);
			}
			else
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot print CA certificate(s) info",
						__function_name);
				zbx_tls_free();
				exit(EXIT_FAILURE);
			}
		}
	}

	/* 'TLSCaFile' parameter (in zabbix_server.conf, zabbix_proxy.conf, zabbix_agentd.conf, zabbix_agent.conf). */
	/* Load CA file if CA path is not configured. */
	if (NULL == ca_cert && NULL != CONFIG_TLS_CA_FILE && '\0' != *CONFIG_TLS_CA_FILE)
	{
		ca_cert = zbx_malloc(ca_cert, sizeof(x509_crt));
		x509_crt_init(ca_cert);

		if (0 != (res = x509_crt_parse_file(ca_cert, CONFIG_TLS_CA_FILE)))
		{
			if (0 > res)
			{
				zbx_tls_error_msg(res, "", &err_msg);
				zabbix_log(LOG_LEVEL_CRIT, "cannot parse CA certificate(s) in file \"%s\": %s",
						CONFIG_TLS_CA_FILE, err_msg);
				zbx_free(err_msg);
			}
			else
			{
				zabbix_log(LOG_LEVEL_CRIT, "cannot parse %d CA certificate(s) in file \"%s\"", res,
						CONFIG_TLS_CA_FILE);
			}

			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_DEBUG))
		{
			/* x509_crt_info() uses a fixed buffer and there is no way to know how large this buffer */
			/* should be to accumulate all info. */
			if (-1 != x509_crt_info(work_buf, sizeof(work_buf), "", ca_cert))
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s(): successfully loaded CA certificate(s) from file "
						"(output may be truncated):\n%s", __function_name, work_buf);
			}
			else
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot print CA certificate(s) info",
						__function_name);
				zbx_tls_free();
				exit(EXIT_FAILURE);
			}
		}
	}

	/* 'TLSCrlFile' parameter (in zabbix_server.conf, zabbix_proxy.conf, zabbix_agentd.conf, zabbix_agent.conf). */
	/* Load CRL (certificate revocation list) file. */
	if (NULL != CONFIG_TLS_CRL_FILE && '\0' != *CONFIG_TLS_CRL_FILE)
	{
		crl = zbx_malloc(crl, sizeof(x509_crl));
		x509_crl_init(crl);

		if (0 != (res = x509_crl_parse_file(crl, CONFIG_TLS_CRL_FILE)))
		{
			if (0 > res)
			{
				zbx_tls_error_msg(res, "", &err_msg);
				zabbix_log(LOG_LEVEL_CRIT, "cannot parse CRL file \"%s\": %s", CONFIG_TLS_CRL_FILE,
						err_msg);
				zbx_free(err_msg);
			}
			else
			{
				zabbix_log(LOG_LEVEL_CRIT, "cannot parse %d certificate(s) in CRL file \"%s\"", res,
						CONFIG_TLS_CRL_FILE);
			}

			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_DEBUG))
		{
			/* x509_crl_info() uses a fixed buffer and there is no way to know how large this buffer */
			/* should be to accumulate all info. */
			if (-1 != x509_crl_info(work_buf, sizeof(work_buf), "", crl))
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s(): successfully loaded CRL from file (output may be "
						"truncated):\n%s", __function_name, work_buf);
			}
			else
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot print CRL info", __function_name);
				zbx_tls_free();
				exit(EXIT_FAILURE);
			}
		}
	}

	/* 'TLSCertFile' parameter (in zabbix_server.conf, zabbix_proxy.conf, zabbix_agentd.conf, zabbix_agent.conf). */
	/* Load certificate. */
	if (NULL != CONFIG_TLS_CERT_FILE && '\0' != *CONFIG_TLS_CERT_FILE)
	{
		my_cert = zbx_malloc(my_cert, sizeof(x509_crt));
		x509_crt_init(my_cert);

		if (0 != (res = x509_crt_parse_file(my_cert, CONFIG_TLS_CERT_FILE)))
		{
			if (0 > res)
			{
				zbx_tls_error_msg(res, "", &err_msg);
				zabbix_log(LOG_LEVEL_CRIT, "cannot parse certificate(s) in file \"%s\": %s",
						CONFIG_TLS_CERT_FILE, err_msg);
				zbx_free(err_msg);
			}
			else
			{
				zabbix_log(LOG_LEVEL_CRIT, "cannot parse %d certificate(s) in file \"%s\"", res,
						CONFIG_TLS_CERT_FILE);
			}

			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_DEBUG))
		{
			/* x509_crt_info() uses a fixed buffer and there is no way to know how large this buffer */
			/* should be to accumulate all info. */
			if (-1 != x509_crt_info(work_buf, sizeof(work_buf), "", my_cert))
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s(): successfully loaded certificate (output may be "
						"truncated):\n%s", __function_name, work_buf);
			}
			else
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot print certificate info", __function_name);
				zbx_tls_free();
				exit(EXIT_FAILURE);
			}
		}
	}

	/* 'TLSKeyFile' parameter (in zabbix_server.conf, zabbix_proxy.conf, zabbix_agentd.conf, zabbix_agent.conf). */
	/* Load private key. */
	if (NULL != CONFIG_TLS_KEY_FILE && '\0' != *CONFIG_TLS_KEY_FILE)
	{
		my_priv_key = zbx_malloc(my_priv_key, sizeof(pk_context));
		pk_init(my_priv_key);

		/* The 3rd argument of pk_parse_keyfile() is password for decrypting the private key. */
		/* Currently the password is not used, it is empty. */
		if (0 != (res = pk_parse_keyfile(my_priv_key, CONFIG_TLS_KEY_FILE, "")))
		{
			zbx_tls_error_msg(res, "", &err_msg);
			zabbix_log(LOG_LEVEL_CRIT, "cannot parse the private key in file \"%s\": %s",
					CONFIG_TLS_KEY_FILE, err_msg);
			zbx_free(err_msg);
			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "%s(): successfully loaded %zu-bit %s private key", __function_name,
				pk_get_size(my_priv_key), pk_get_name(my_priv_key));
	}

	/* 'TLSPskFile' parameter (in zabbix_server.conf, zabbix_proxy.conf, zabbix_agentd.conf, zabbix_agent.conf). */
	/* Load pre-shared key. */
	if (NULL != CONFIG_TLS_PSK_FILE && '\0' != *CONFIG_TLS_PSK_FILE)
	{
		FILE		*f;
		size_t		len;
		char		*p;
		int		len_bin;
		unsigned int	i;
		char		buf[HOST_TLS_PSK_LEN_MAX + 2];	/* up to 512 bytes of hex-digits, maybe 1-2 bytes */
								/* for '\n', 1 byte for terminating '\0' */
		char		buf_bin[HOST_TLS_PSK_LEN/2];	/* up to 256 bytes of binary PSK */

		if (NULL == (f = fopen(CONFIG_TLS_PSK_FILE, "r")))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot open file \"%s\": %s", CONFIG_TLS_PSK_FILE,
					zbx_strerror(errno));
			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		if (NULL == fgets(buf, (int)sizeof(buf), f))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot read from file \"%s\": %s", CONFIG_TLS_PSK_FILE,
					zbx_strerror(errno));
			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		if (0 != fclose(f))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot close file \"%s\": %s", CONFIG_TLS_PSK_FILE,
					zbx_strerror(errno));
			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		if (NULL != (p = strchr(buf, '\n')))
			*p = '\0';

		if (0 == (len = strlen(buf)))
		{
			zabbix_log(LOG_LEVEL_CRIT, "file \"%s\" is empty", CONFIG_TLS_PSK_FILE);
			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		/* Currently PolarSSL supports up to 32 bytes long PSKs, but with other libraries we can support up */
		/* to 256 bytes. Check both limits for safer code. */
		if (POLARSSL_PSK_MAX_LEN * 2 < len || HOST_TLS_PSK_LEN < len)
		{
			zabbix_log(LOG_LEVEL_CRIT, "PSK in file \"%s\" is too large", CONFIG_TLS_PSK_FILE);
			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		if (0 >= (len_bin = zbx_psk_hex2bin((unsigned char *)buf, (unsigned char *)buf_bin, sizeof(buf_bin))))
		{
			zabbix_log(LOG_LEVEL_CRIT, "invalid PSK in file \"%s\"", CONFIG_TLS_PSK_FILE);
			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		my_psk_len = (size_t)len_bin;
		my_psk = zbx_malloc(my_psk, my_psk_len);

		for (i = 0; i < my_psk_len; i++)
			my_psk[i] = buf_bin[i];

		zabbix_log(LOG_LEVEL_DEBUG, "%s(): successfully loaded pre-shared key", __function_name);
	}

	/* 'TLSPskIdentity' parameter (in zabbix_server.conf, zabbix_proxy.conf, zabbix_agentd.conf, */
	/* zabbix_agent.conf). Configure identity to be used with the pre-shared key. */
	if (NULL != CONFIG_TLS_PSK_IDENTITY && '\0' != *CONFIG_TLS_PSK_IDENTITY)
	{
		/* PSK identity must be a valid UTF-8 string (RFC4279 says Unicode) */
		if (SUCCEED != zbx_is_utf8(CONFIG_TLS_PSK_IDENTITY))
		{
			zabbix_log(LOG_LEVEL_CRIT, "configuration parameter \"TLSPskIdentity\" value is not a valid "
					"UTF-8 string");
			zbx_tls_free();
			exit(EXIT_FAILURE);
		}

		my_psk_identity = CONFIG_TLS_PSK_IDENTITY;
		my_psk_identity_len = strlen(my_psk_identity);

		zabbix_log(LOG_LEVEL_DEBUG, "%s(): successfully loaded pre-shared key\'s identity", __function_name);
	}

	zbx_tls_validate_config();

	/* Certificate always comes from configuration file. Set up ciphersuites. */
	if (NULL != my_cert)
	{
		cipher_count = zbx_ciphersuites(ZBX_TLS_CIPHERSUITE_CERT, &ciphersuites_cert);
		zabbix_log(LOG_LEVEL_DEBUG, "%s(): set up a list of %u certificate ciphersuites", __function_name,
				cipher_count);
	}

	/* PSK can come from configuration file (in proxy, agentd, agent) and later from database (in server, proxy). */
	/* Configure ciphersuites just in case they will be used. */
	if (NULL != my_psk || 0 != (program_type & (ZBX_PROGRAM_TYPE_SERVER | ZBX_PROGRAM_TYPE_PROXY)))
	{
		cipher_count = zbx_ciphersuites(ZBX_TLS_CIPHERSUITE_PSK, &ciphersuites_psk);
		zabbix_log(LOG_LEVEL_DEBUG, "%s(): set up a list of %u PSK ciphersuites", __function_name,
				cipher_count);
	}

	/* Sometimes we need to be ready for both certificate and PSK whichever comes in. Set up a combined list of */
	/* ciphersuites. */
	if (NULL != my_cert && (NULL != my_psk ||
			0 != (program_type & (ZBX_PROGRAM_TYPE_SERVER | ZBX_PROGRAM_TYPE_PROXY))))
	{
		cipher_count = zbx_ciphersuites(ZBX_TLS_CIPHERSUITE_ALL, &ciphersuites_all);
		zabbix_log(LOG_LEVEL_DEBUG, "%s(): set up a list of %u certificate and PSK ciphersuites",
				__function_name, cipher_count);
	}

	entropy = zbx_malloc(entropy, sizeof(entropy_context));
	entropy_init(entropy);

	zbx_tls_make_personalization_string(&pers, &pers_len);

	ctr_drbg = zbx_malloc(ctr_drbg, sizeof(ctr_drbg_context));

	if (0 != (res = ctr_drbg_init(ctr_drbg, entropy_func, entropy, (unsigned char *)pers, pers_len)))
	{
		zbx_tls_error_msg(res, "", &err_msg);
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize random number generator: %s", err_msg);
		zbx_free(err_msg);
		zbx_tls_free();
		exit(EXIT_FAILURE);
	}

	zbx_guaranteed_memset(pers, 0, pers_len);
	zbx_free(pers);

#elif defined(HAVE_GNUTLS)
	if (GNUTLS_E_SUCCESS != gnutls_global_init())
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize GnuTLS library");
		exit(EXIT_FAILURE);
	}
#elif defined(HAVE_OPENSSL)
	SSL_load_error_strings();
	SSL_library_init();
#endif
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tls_free                                                     *
 *                                                                            *
 * Purpose: release TLS library resources allocated in zbx_tls_init_parent()  *
 *          and zbx_tls_init_child()                                          *
 *                                                                            *
 ******************************************************************************/
int	zbx_tls_free(void)
{
	const char	*__function_name = "zbx_tls_free";
	int		ret = SUCCEED;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);
#if defined(HAVE_POLARSSL)
	if (NULL != ctr_drbg)
	{
		ctr_drbg_free(ctr_drbg);
		zbx_free(ctr_drbg);
	}

	if (NULL != entropy)
	{
		entropy_free(entropy);
		zbx_free(entropy);
	}

	if (NULL != my_psk)
	{
		zbx_guaranteed_memset(my_psk, 0, my_psk_len);
		my_psk_len = 0;
		zbx_free(my_psk);
	}

	if (NULL != my_priv_key)
	{
		pk_free(my_priv_key);
		zbx_free(my_priv_key);
	}

	if (NULL != my_cert)
	{
		x509_crt_free(my_cert);
		zbx_free(my_cert);
	}

	if (NULL != crl)
	{
		x509_crl_free(crl);
		zbx_free(crl);
	}

	if (NULL != ca_cert)
	{
		x509_crt_free(ca_cert);
		zbx_free(ca_cert);
	}

	zbx_free(ciphersuites_psk);
	zbx_free(ciphersuites_cert);
	zbx_free(ciphersuites_all);
#elif defined(HAVE_GNUTLS)
	gnutls_global_deinit();
#elif defined(HAVE_OPENSSL)
	/* TODO there is no ERR_free_strings() in my libssl. Commented out temporarily. */
	/* ERR_free_strings(); */
#endif
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tls_connect                                                  *
 *                                                                            *
 * Purpose: establish a TLS connection over an established TCP connection     *
 *                                                                            *
 * Parameters:                                                                *
 *     s           - [IN]  socket with opened connection                      *
 *     error       - [OUT] dynamically allocated memory with error message    *
 *     tls_connect - [IN]  how to connect. Allowed values:                    *
 *                         ZBX_TCP_SEC_TLS_CERT, ZBX_TCP_SEC_TLS_PSK.         *
 *     tls_arg1    - [IN]  'tls_issuer' or 'tls_psk_identity' depending on    *
 *                         value of 'tls_connect'.                            *
 *     tls_arg2    - [IN]  'tls_subject' or 'tls_psk' depending on value of   *
 *                         'tls_connect'.                                     *
 *                                                                            *
 * Return value: SUCCEED on successful TLS handshake with a valid certificate *
 *               or PSK                                                       *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 ******************************************************************************/
int	zbx_tls_connect(zbx_sock_t *s, char **error, unsigned int tls_connect, char *tls_arg1, char *tls_arg2)
{
	const char	*__function_name = "zbx_tls_connect";
	int		ret = FAIL;

#if defined(HAVE_POLARSSL)
	int		res;
	const x509_crt	*peer_cert;
#endif
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

#if defined(HAVE_POLARSSL)
	if (ZBX_TCP_SEC_TLS_CERT == tls_connect)
	{
		if (NULL == ciphersuites_cert)
		{
			*error = zbx_strdup(*error, "cannot connect with TLS and certificate: no valid certificate "
					"loaded");
			goto out;
		}
	}
	else if (NULL == ciphersuites_psk)	/* use a pre-shared key */
	{
		*error = zbx_strdup(*error, "cannot connect with TLS and PSK: no valid PSK loaded");
		goto out;
	}

	/* set up TLS context */
	s->tls_ctx = zbx_malloc(s->tls_ctx, sizeof(ssl_context));
	memset(s->tls_ctx, 0, sizeof(ssl_context));

	if (0 != (res = ssl_init(s->tls_ctx)))
	{
		zbx_tls_error_msg(res, "ssl_init(): ", error);
		ssl_free(s->tls_ctx);
		zbx_free(s->tls_ctx);
		goto out;
	}

	ssl_set_endpoint(s->tls_ctx, SSL_IS_CLIENT);

	/* Set RNG callback where to get random numbers from */
	ssl_set_rng(s->tls_ctx, ctr_drbg_random, ctr_drbg);

	if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_TRACE))
	{
		/* Set our own debug function 'polarssl_debug()' as a callback function. The 3rd parameter of */
		/* ssl_set_dbg() we set to NULL. During a calback, the 3rd parameter will be passed to our function */
		/* 'polarssl_debug()' as the 1st parameter, but it will be ignored in 'polarssl_debug()'. */
		ssl_set_dbg(s->tls_ctx, polarssl_debug, NULL);

		/* For Zabbix LOG_LEVEL_TRACE, PolarSSL debug level 3 seems the best. Recompile with 4 (apparently */
		/* the highest PolarSSL debug level) to dump also network raw bytes. */
		debug_set_threshold(3);
	}

	/* Set callback functions for receiving and sending data via socket. */
	/* Functions provided by PolarSSL work well so far, no need to invent our own. */
	ssl_set_bio(s->tls_ctx, net_recv, &s->socket, net_send, &s->socket);

	/* set protocol version to TLS 1.2 */
	ssl_set_min_version(s->tls_ctx, ZBX_TLS_MIN_MAJOR_VER, ZBX_TLS_MIN_MINOR_VER);
	ssl_set_max_version(s->tls_ctx, ZBX_TLS_MAX_MAJOR_VER, ZBX_TLS_MAX_MINOR_VER);

	if (ZBX_TCP_SEC_TLS_CERT == tls_connect)	/* use certificates */
	{
		ssl_set_authmode(s->tls_ctx, SSL_VERIFY_REQUIRED);
		ssl_set_ciphersuites(s->tls_ctx, ciphersuites_cert);

		/* set CA certificate and certificate revocation lists */
		ssl_set_ca_chain(s->tls_ctx, ca_cert, crl, NULL);	/* TODO set the 4th argument to expected peer Common Name */

		if (0 != (res = ssl_set_own_cert(s->tls_ctx, my_cert, my_priv_key)))
		{
			zbx_tls_error_msg(res, "ssl_set_own_cert(): ", error);
			ssl_free(s->tls_ctx);
			zbx_free(s->tls_ctx);
			goto out;
		}
	}
	else	/* use a pre-shared key */
	{
		ssl_set_ciphersuites(s->tls_ctx, ciphersuites_psk);

		if (NULL == tls_arg1)	/* 'tls_psk_identity' is not set */
		{
			/* set up the PSK from a configuration file (in agentd, agent (always) and a case in active */
			/* proxy when it connects to server) */

			if (0 != (res = ssl_set_psk(s->tls_ctx, (const unsigned char *)my_psk, my_psk_len,
				(const unsigned char *)my_psk_identity, my_psk_identity_len)))
			{
				zbx_tls_error_msg(res, "ssl_set_psk(): ", error);
				ssl_free(s->tls_ctx);
				zbx_free(s->tls_ctx);
				goto out;
			}
		}
		else
		{
			/* PSK comes from a database (case for a server/proxy when it connects to an agent for */
			/* passive checks, for a server when it connects to a passive proxy) */

			int	psk_len;
			char	psk_buf[HOST_TLS_PSK_LEN/2];

			if (0 >= (psk_len = zbx_psk_hex2bin((unsigned char *)tls_arg2, (unsigned char *)psk_buf,
					sizeof(psk_buf))))
			{
				*error = zbx_strdup(*error, "invalid pre-shared key");
				ssl_free(s->tls_ctx);
				zbx_free(s->tls_ctx);
				goto out;
			}

			if (0 != (res = ssl_set_psk(s->tls_ctx, (const unsigned char *)psk_buf, (size_t)psk_len,
				(const unsigned char *)tls_arg1, strlen(tls_arg1))))
			{
				zbx_tls_error_msg(res, "ssl_set_psk(): ", error);
				ssl_free(s->tls_ctx);
				zbx_free(s->tls_ctx);
				goto out;
			}
		}
	}

	while (0 != (res = ssl_handshake(s->tls_ctx)))
	{
		if (POLARSSL_ERR_NET_WANT_READ != res && POLARSSL_ERR_NET_WANT_WRITE != res)
		{
			zbx_tls_error_msg(res, "ssl_handshake(): ", error);
			ssl_free(s->tls_ctx);
			zbx_free(s->tls_ctx);
			goto out;
		}
	}

	if (ZBX_TCP_SEC_TLS_CERT == tls_connect)
	{
		if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_DEBUG))
		{
			/* log peer certificate information for debugging */

			if (NULL == (peer_cert = ssl_get_peer_cert(s->tls_ctx)) || -1 == x509_crt_info(work_buf,
					sizeof(work_buf), "", peer_cert))
			{
				*error = zbx_strdup(*error, "cannot get peer certificate info");
				ssl_free(s->tls_ctx);
				zbx_free(s->tls_ctx);
				goto out;
			}

			zabbix_log(LOG_LEVEL_DEBUG, "%s(): peer certificate:\n%s", __function_name, work_buf);
		}

		/* validate peer certificate TODO: Issuer and Subject validation */

		if (0 != (res = ssl_get_verify_result(s->tls_ctx)))
		{
			int	k = 0;

			*error = zbx_strdup(*error, "invalid peer certificate: ");

			if (0 != (BADCERT_EXPIRED & res))
			{
				*error = zbx_strdcat(*error, "expired");
				k++;
			}

			if (0 != (BADCERT_REVOKED & res))
			{
				if (0 != k)
					*error = zbx_strdcat(*error, ", ");

				*error = zbx_strdcat(*error, "revoked");
				k++;
			}

			if (0 != (BADCERT_CN_MISMATCH & res))
			{
				if (0 != k)
					*error = zbx_strdcat(*error, ", ");

				*error = zbx_strdcat(*error, "Common Name mismatch");
				k++;
			}

			if (0 != (BADCERT_NOT_TRUSTED & res))
			{
				if (0 != k)
					*error = zbx_strdcat(*error, ", ");

				*error = zbx_strdcat(*error, "self-signed or not signed by a trusted CA");
			}

			ssl_free(s->tls_ctx);
			zbx_free(s->tls_ctx);
			goto out;
		}

		s->connection_type = ZBX_TCP_SEC_TLS_CERT;
	}
	else	/* pre-shared key */
	{
		s->connection_type = ZBX_TCP_SEC_TLS_PSK;

		if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_DEBUG))
		{
			/* special print: s->tls_ctx->psk_identity is not '\0'-terminated */
			zabbix_log(LOG_LEVEL_DEBUG, "%s(): PSK-identity: \"%.*s\"", __function_name,
					s->tls_ctx->psk_identity_len, s->tls_ctx->psk_identity);
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s(): SUCCEED (established %s %s)", __function_name,
			ssl_get_version(s->tls_ctx), ssl_get_ciphersuite(s->tls_ctx));

	return SUCCEED;
out:
#endif
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s:%s", __function_name, zbx_result_string(ret),
			ZBX_NULL2EMPTY_STR(*error));
	return ret;
}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
/******************************************************************************
 *                                                                            *
 * Function: zbx_tls_accept                                                   *
 *                                                                            *
 * Purpose: establish a TLS connection over an accepted TCP connection        *
 *                                                                            *
 * Parameters:                                                                *
 *     s          - [IN]  socket with opened connection                       *
 *     error      - [OUT] dynamically allocated memory with error message     *
 *     tls_accept - [IN]  what connection to accept. Can be be either         *
 *                        ZBX_TCP_SEC_TLS_CERT or ZBX_TCP_SEC_TLS_PSK, or     *
 *                        a bitwise 'OR' of both.                             *
 *                                                                            *
 * Return value: SUCCEED on successful TLS handshake with a valid certificate *
 *               or PSK                                                       *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 ******************************************************************************/
int	zbx_tls_accept(zbx_sock_t *s, char **error, unsigned int tls_accept)
{
	const char		*__function_name = "zbx_tls_accept";
	const ssl_ciphersuite_t	*info;
	int			ret = FAIL;

#if defined(HAVE_POLARSSL)
	int			res;
	const x509_crt		*peer_cert;
#endif
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

#if defined(HAVE_POLARSSL)
	if (0 != (tls_accept & ZBX_TCP_SEC_TLS_PSK) && NULL == ciphersuites_psk)
	{
		*error = zbx_strdup(*error, "cannot accept TLS connection with PSK: no valid PSK loaded");
		goto out;
	}

	/* set up TLS context */
	s->tls_ctx = zbx_malloc(s->tls_ctx, sizeof(ssl_context));
	memset(s->tls_ctx, 0, sizeof(ssl_context));

	if (0 != (res = ssl_init(s->tls_ctx)))
	{
		zbx_tls_error_msg(res, "ssl_init(): ", error);
		ssl_free(s->tls_ctx);
		zbx_free(s->tls_ctx);
		goto out;
	}

	ssl_set_endpoint(s->tls_ctx, SSL_IS_SERVER);

	/* Set RNG callback where to get random numbers from */
	ssl_set_rng(s->tls_ctx, ctr_drbg_random, ctr_drbg);

	if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_TRACE))
	{
		/* Set our own debug function 'polarssl_debug()' as a callback function. The 3rd parameter of */
		/* ssl_set_dbg() we set to NULL. During a calback, the 3rd parameter will be passed to our function */
		/* 'polarssl_debug()' as the 1st parameter, but it will be ignored in 'polarssl_debug()'. */
		ssl_set_dbg(s->tls_ctx, polarssl_debug, NULL);

		/* For Zabbix LOG_LEVEL_TRACE, PolarSSL debug level 3 seems the best. Recompile with 4 (apparently */
		/* the highest PolarSSL debug level) to dump also network raw bytes. */
		debug_set_threshold(3);
	}

	/* Set callback functions for receiving and sending data via socket. */
	/* Functions provided by PolarSSL work well so far, no need to invent our own. */
	ssl_set_bio(s->tls_ctx, net_recv, &s->socket, net_send, &s->socket);

	/* set protocol version to TLS 1.2 */
	ssl_set_min_version(s->tls_ctx, ZBX_TLS_MIN_MAJOR_VER, ZBX_TLS_MIN_MINOR_VER);
	ssl_set_max_version(s->tls_ctx, ZBX_TLS_MAX_MAJOR_VER, ZBX_TLS_MAX_MINOR_VER);

	if (0 != (tls_accept & ZBX_TCP_SEC_TLS_CERT))	/* prepare to accept with a certificate */
	{
		ssl_set_authmode(s->tls_ctx, SSL_VERIFY_REQUIRED);

		/* set CA certificate and certificate revocation lists */
		if (NULL != ca_cert)
			ssl_set_ca_chain(s->tls_ctx, ca_cert, crl, NULL);	/* TODO set the 4th argument to expected peer Common Name ? */

		if (NULL != my_cert && 0 != (res = ssl_set_own_cert(s->tls_ctx, my_cert, my_priv_key)))
		{
			zbx_tls_error_msg(res, "ssl_set_own_cert(): ", error);
			ssl_free(s->tls_ctx);
			zbx_free(s->tls_ctx);
			goto out;
		}
	}

	if (0 != (tls_accept & ZBX_TCP_SEC_TLS_PSK))	/* prepare te accept with a pre-shared key */
	{
		/* for agentd and agent the only possibility is a PSK from configuration file */
		if ((0 != (program_type & ZBX_PROGRAM_TYPE_AGENTD) ||
				0 != (program_type & ZBX_PROGRAM_TYPE_AGENT)) &&
				0 != (res = ssl_set_psk(s->tls_ctx, (const unsigned char *)my_psk, my_psk_len,
				(const unsigned char *)my_psk_identity, my_psk_identity_len)))
		{
			zbx_tls_error_msg(res, "ssl_set_psk(): ", error);
			ssl_free(s->tls_ctx);
			zbx_free(s->tls_ctx);
			goto out;
		}
		else if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY) ||
				0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
		{
			/* For server or proxy a PSK can come from configuration file or database. */
			/* Set up a callback function for finding the requested PSK. */
			ssl_set_psk_cb(s->tls_ctx, zbx_psk_callback, NULL);
		}
	}

	if ((ZBX_TCP_SEC_TLS_CERT | ZBX_TCP_SEC_TLS_PSK) == (tls_accept & (ZBX_TCP_SEC_TLS_CERT | ZBX_TCP_SEC_TLS_PSK)))
	{
		/* common case in trapper - be ready for all types of incoming connections */
		if (NULL != my_cert)
		{
			/* it can also be a case in agentd listener - when both certificate and PSK is allowed, e.g. */
			/* for switching of TLS connections from PSK to using a certificate */
			ssl_set_ciphersuites(s->tls_ctx, ciphersuites_all);
		}
		else
		{
			/* assume PSK, although it is not yet known will there be the right PSK available */
			ssl_set_ciphersuites(s->tls_ctx, ciphersuites_psk);
		}
	}
	else if (0 != (tls_accept & ZBX_TCP_SEC_TLS_CERT) && NULL != my_cert)
		ssl_set_ciphersuites(s->tls_ctx, ciphersuites_cert);
	else if (0 != (tls_accept & ZBX_TCP_SEC_TLS_PSK))
		ssl_set_ciphersuites(s->tls_ctx, ciphersuites_psk);

	while (0 != (res = ssl_handshake(s->tls_ctx)))
	{
		if (POLARSSL_ERR_NET_WANT_READ != res && POLARSSL_ERR_NET_WANT_WRITE != res)
		{
			zbx_tls_error_msg(res, "ssl_handshake(): ", error);
			ssl_free(s->tls_ctx);
			zbx_free(s->tls_ctx);
			goto out;
		}
	}

	/* Is this TLS conection using a certificate or a PSK ? */

	info = ssl_ciphersuite_from_id(s->tls_ctx->session->ciphersuite);

	if (POLARSSL_KEY_EXCHANGE_PSK == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_DHE_PSK == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_ECDHE_PSK == info->key_exchange ||
			POLARSSL_KEY_EXCHANGE_RSA_PSK == info->key_exchange)
	{
		s->connection_type = ZBX_TCP_SEC_TLS_PSK;

		if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_DEBUG))
		{
			/* special print: s->tls_ctx->psk_identity is not '\0'-terminated */
			zabbix_log(LOG_LEVEL_DEBUG, "%s(): PSK-identity: \"%.*s\"", __function_name,
					s->tls_ctx->psk_identity_len, s->tls_ctx->psk_identity);
		}
	}
	else
	{
		s->connection_type = ZBX_TCP_SEC_TLS_CERT;

		if (SUCCEED == zabbix_check_log_level(LOG_LEVEL_DEBUG))
		{
			/* log peer certificate information for debugging */

			if (NULL == (peer_cert = ssl_get_peer_cert(s->tls_ctx)) || -1 == x509_crt_info(work_buf,
					sizeof(work_buf), "", peer_cert))
			{
				*error = zbx_strdup(*error, "cannot get peer certificate info");
				ssl_free(s->tls_ctx);
				zbx_free(s->tls_ctx);
				goto out;
			}

			zabbix_log(LOG_LEVEL_DEBUG, "%s(): peer certificate:\n%s", __function_name, work_buf);
		}

		/* validate peer certificate TODO: Issuer and Subject validation */

		if (0 != (res = ssl_get_verify_result(s->tls_ctx)))
		{
			int	k = 0;

			*error = zbx_strdup(*error, "invalid peer certificate: ");

			if (0 != (BADCERT_EXPIRED & res))
			{
				*error = zbx_strdcat(*error, "expired");
				k++;
			}

			if (0 != (BADCERT_REVOKED & res))
			{
				if (0 != k)
					*error = zbx_strdcat(*error, ", ");

				*error = zbx_strdcat(*error, "revoked");
				k++;
			}

			if (0 != (BADCERT_CN_MISMATCH & res))
			{
				if (0 != k)
					*error = zbx_strdcat(*error, ", ");

				*error = zbx_strdcat(*error, "Common Name mismatch");
				k++;
			}

			if (0 != (BADCERT_NOT_TRUSTED & res))
			{
				if (0 != k)
					*error = zbx_strdcat(*error, ", ");

				*error = zbx_strdcat(*error, "self-signed or not signed by a trusted CA");
			}

			ssl_free(s->tls_ctx);
			zbx_free(s->tls_ctx);
			goto out;
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s(): SUCCEED (established %s %s)", __function_name,
			ssl_get_version(s->tls_ctx), ssl_get_ciphersuite(s->tls_ctx));

	return SUCCEED;
out:
#endif
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s:%s", __function_name, zbx_result_string(ret),
			ZBX_NULL2EMPTY_STR(*error));
	return ret;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_tls_close                                                    *
 *                                                                            *
 * Purpose: close a TLS connection before closing a TCP socket                *
 *                                                                            *
 ******************************************************************************/
void	zbx_tls_close(zbx_sock_t *s)
{
#if defined(HAVE_POLARSSL)
	if (NULL != s->tls_ctx)
	{
		ssl_close_notify(s->tls_ctx);
		ssl_free(s->tls_ctx);
		zbx_free(s->tls_ctx);
	}
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tls_connection_type_name                                     *
 *                                                                            *
 * Purpose: translate connection type code to name                            *
 *                                                                            *
 ******************************************************************************/
const char	*zbx_tls_connection_type_name(unsigned int type)
{
	static const char	*unencrypted = "unencrypted";
	static const char	*tls_psk = "TLS with PSK";
	static const char	*tls_cert = "TLS with certificate";
	static const char	*unknown = "unknown";

	if (ZBX_TCP_SEC_UNENCRYPTED == type)
		return unencrypted;
	else if (ZBX_TCP_SEC_TLS_CERT == type)
		return tls_cert;
	else if (ZBX_TCP_SEC_TLS_PSK == type)
		return tls_psk;
	else
		return unknown;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tls_get_attr                                                 *
 *                                                                            *
 * Purpose: get connection type, certificate and PSK attributes from a        *
 *          context of established connection                                 *
 *                                                                            *
 ******************************************************************************/
int	zbx_tls_get_attr(const zbx_sock_t *s, zbx_tls_conn_attr_t *attr)
{
	attr->connection_type = s->connection_type;

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (ZBX_TCP_SEC_UNENCRYPTED == s->connection_type)
	{
		attr->arg1 = NULL;
		attr->arg1_len = 0;
		attr->arg2 = NULL;
		attr->arg2_len = 0;
	}
#endif
#if defined(HAVE_POLARSSL)
	else if (ZBX_TCP_SEC_TLS_CERT == s->connection_type)
	{
		const x509_crt	*peer_cert;

		if (NULL == (peer_cert = ssl_get_peer_cert(s->tls_ctx)))
			return FAIL;

		/* TODO implement getting pointers to issuer and subject and their lengths */
		return FAIL;
	}
	else if (ZBX_TCP_SEC_TLS_PSK == s->connection_type)
	{
		attr->arg1 = (char *)s->tls_ctx->psk_identity;
		attr->arg1_len = s->tls_ctx->psk_identity_len;
		attr->arg2 = NULL;
		attr->arg2_len = 0;
	}
#endif
	return SUCCEED;
}
