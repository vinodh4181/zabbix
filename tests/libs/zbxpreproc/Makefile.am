if SERVER
SERVER_tests = zbx_item_preproc
SERVER_tests += item_preproc_csv_to_json

if HAVE_LIBXML2
SERVER_tests +=	item_preproc_xpath
endif

noinst_PROGRAMS = $(SERVER_tests)

COMMON_SRC_FILES = \
	../../zbxmocktest.h

JSON_LIBS = \
	$(top_srcdir)/src/libs/zbxpreproc/libzbxpreproc.a \
	$(top_srcdir)/src/libs/zbxtimekeeper/libzbxtimekeeper.a \
	$(top_srcdir)/src/libs/zbxipcservice/libzbxipcservice.a \
	$(top_srcdir)/src/libs/zbxembed/libzbxembed.a \
	$(top_srcdir)/src/libs/zbxjson/libzbxjson.a \
	$(top_srcdir)/src/libs/zbxcomms/libzbxcomms.a \
	$(top_srcdir)/src/libs/zbxcompress/libzbxcompress.a \
	$(top_srcdir)/src/libs/zbxregexp/libzbxregexp.a \
	$(top_srcdir)/src/libs/zbxxml/libzbxxml.a \
	$(top_srcdir)/src/libs/zbxvariant/libzbxvariant.a \
	$(top_srcdir)/src/libs/zbxhash/libzbxhash.a \
	$(top_srcdir)/src/libs/zbxcrypto/libzbxcrypto.a \
	$(top_srcdir)/src/libs/zbxprometheus/libzbxprometheus.a \
	$(top_srcdir)/src/libs/zbxeval/libzbxeval.a \
	$(top_srcdir)/src/libs/zbxserialize/libzbxserialize.a \
	$(top_srcdir)/src/libs/zbxhttp/libzbxhttp.a \
	$(top_srcdir)/src/libs/zbxlog/libzbxlog.a \
	$(top_srcdir)/src/libs/zbxconf/libzbxconf.a \
	$(top_srcdir)/src/libs/zbxthreads/libzbxthreads.a \
	$(top_srcdir)/src/libs/zbxtime/libzbxtime.a \
	$(top_srcdir)/src/libs/zbxmutexs/libzbxmutexs.a \
	$(top_srcdir)/src/libs/zbxprof/libzbxprof.a \
	$(top_srcdir)/src/libs/zbxalgo/libzbxalgo.a \
	$(top_srcdir)/src/libs/zbxip/libzbxip.a \
	$(top_srcdir)/src/libs/zbxnix/libzbxnix.a \
	$(top_srcdir)/src/libs/zbxstr/libzbxstr.a \
	$(top_srcdir)/src/libs/zbxnum/libzbxnum.a \
	$(top_srcdir)/src/libs/zbxcommon/libzbxcommon.a \
	$(top_srcdir)/tests/libzbxmocktest.a \
	$(top_srcdir)/tests/libzbxmockdata.a

zbx_item_preproc_SOURCES = \
	zbx_item_preproc.c
	
zbx_item_preproc_LDADD = $(JSON_LIBS)

zbx_item_preproc_LDADD += @SERVER_LIBS@
zbx_item_preproc_LDFLAGS = @SERVER_LDFLAGS@

zbx_item_preproc_CFLAGS = -I@top_srcdir@/tests -I@top_srcdir@/src @LIBXML2_CFLAGS@

item_preproc_xpath_SOURCES = \
	item_preproc_xpath.c \
	$(COMMON_SRC_FILES) 

item_preproc_xpath_LDADD = $(JSON_LIBS)

item_preproc_xpath_LDADD += @SERVER_LIBS@
item_preproc_xpath_LDFLAGS = @SERVER_LDFLAGS@

item_preproc_xpath_CFLAGS = -I@top_srcdir@/tests -I@top_srcdir@/src @LIBXML2_CFLAGS@

item_preproc_csv_to_json_SOURCES = \
	item_preproc_csv_to_json.c \
	$(COMMON_SRC_FILES)

item_preproc_csv_to_json_LDADD = $(JSON_LIBS)

item_preproc_csv_to_json_LDADD += @SERVER_LIBS@
item_preproc_csv_to_json_LDFLAGS = @SERVER_LDFLAGS@

item_preproc_csv_to_json_CFLAGS = -I@top_srcdir@/tests -I@top_srcdir@/src @LIBXML2_CFLAGS@

endif
