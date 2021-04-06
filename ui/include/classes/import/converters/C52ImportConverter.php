<?php declare(strict_types = 1);
/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
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


/**
 * Converter for converting import data from 5.2 to 5.4.
 */
class C52ImportConverter extends CConverter {

	/**
	 * Converter used to convert trigger expressions from 5.2 to 5.4 syntax.
	 *
	 * @var C52TriggerExpressionConverter
	 */
	protected $trigger_expression_converter;

	public function __construct() {
		$this->trigger_expression_converter = new C52TriggerExpressionConverter();
	}

	/**
	 * Convert import data from 5.2 to 5.4 version.
	 *
	 * @param array $data
	 *
	 * @return array
	 */
	public function convert($data): array {
		$data['zabbix_export']['version'] = '5.4';

		if (array_key_exists('hosts', $data['zabbix_export'])) {
			$data['zabbix_export']['hosts'] = $this->convertHosts($data['zabbix_export']['hosts']);
		}

		if (array_key_exists('templates', $data['zabbix_export'])) {
			$data['zabbix_export']['templates'] = $this->convertTemplates($data['zabbix_export']['templates']);
		}

		if (array_key_exists('triggers', $data['zabbix_export'])) {
			foreach ($data['zabbix_export']['triggers'] as &$trigger) {
				$trigger = $this->convertTrigger($trigger);
			}
			unset($trigger);
		}

		if (array_key_exists('value_maps', $data['zabbix_export'])) {
			$data['zabbix_export'] = self::convertValueMaps($data['zabbix_export']);
		}

		return $data;
	}

	/**
	 * Convert hosts.
	 *
	 * @param array $hosts
	 *
	 * @return array
	 */
	private function convertHosts(array $hosts): array {
		$parser = new C52CalculatedItemConverter();
		$tls_fields = array_flip(['tls_connect', 'tls_accept', 'tls_issuer', 'tls_subject', 'tls_psk_identity',
			'tls_psk'
		]);

		foreach ($hosts as &$host) {
			$host = array_diff_key($host, $tls_fields);

			if (array_key_exists('items', $host)) {
				foreach ($host['items'] as &$item) {
					if (array_key_exists('triggers', $item)) {
						foreach ($item['triggers'] as &$trigger) {
							$trigger = $this->convertTrigger($trigger, $host['host'], $item['key']);
						}
						unset($trigger);
					}

					if ($item['type'] === CXmlConstantName::CALCULATED) {
						$item = $parser->convert($item);
					}
				}
				unset($item);
			}

			if (array_key_exists('interfaces', $host)) {
				$host['interfaces'] = self::convertIntefaces($host['interfaces']);
			}

			if (array_key_exists('discovery_rules', $host)) {
				$host['discovery_rules'] = $this->convertDiscoveryRules($host['discovery_rules'], $host['host']);
			}
		}
		unset($host);

		return $hosts;
	}

	/**
	 * Convert templates.
	 *
	 * @param array $templates
	 *
	 * @return array
	 */
	private function convertTemplates(array $templates): array {
		$parser = new C52CalculatedItemConverter();

		foreach ($templates as &$template) {
			if (array_key_exists('items', $template)) {
				foreach ($template['items'] as &$item) {
					if (array_key_exists('triggers', $item)) {
						foreach ($item['triggers'] as &$trigger) {
							$trigger = $this->convertTrigger($trigger, $template['template'], $item['key']);
						}
						unset($trigger);
					}

					if ($item['type'] === CXmlConstantName::CALCULATED) {
						$item = $parser->convert($item);
					}
				}
				unset($item);
			}

			if (array_key_exists('discovery_rules', $template)) {
				$template['discovery_rules'] = $this->convertDiscoveryRules($template['discovery_rules'],
					$template['template']
				);
			}
		}
		unset($template);

		return $templates;
	}

	/**
	 * Convert interfaces.
	 *
	 * @static
	 *
	 * @param array $interfaces
	 *
	 * @return array
	 */
	private static function convertIntefaces(array $interfaces): array {
		$result = [];

		foreach ($interfaces as $interface) {
			$snmp_v3_convert = array_key_exists('type', $interface)
				&& $interface['type'] === CXmlConstantName::SNMP
				&& array_key_exists('details', $interface)
				&& array_key_exists('version', $interface['details'])
				&& $interface['details']['version'] === CXmlConstantName::SNMPV3;

			if ($snmp_v3_convert) {
				if (array_key_exists('authprotocol', $interface['details'])
						&& $interface['details']['authprotocol'] === CXmlConstantName::SHA) {
					$interface['details']['authprotocol'] = CXmlConstantName::SHA1;
				}

				if (array_key_exists('privprotocol', $interface['details'])
						&& $interface['details']['privprotocol'] === CXmlConstantName::AES) {
					$interface['details']['privprotocol'] = CXmlConstantName::AES128;
				}
			}

			$result[] = $interface;
		}

		return $result;
	}

	/**
	 * Convert discover rules.
	 *
	 * @param array  $discovery_rules
	 * @param string $hostname
	 *
	 * @return array
	 */
	private function convertDiscoveryRules(array $discovery_rules, string $hostname): array {
		$result = [];

		foreach ($discovery_rules as $discovery_rule) {
			if (array_key_exists('host_prototypes', $discovery_rule)) {
				$discovery_rule['host_prototypes'] = self::convertHostPrototypes($discovery_rule['host_prototypes']);
			}

			if (array_key_exists('item_prototypes', $discovery_rule)) {
				$discovery_rule['item_prototypes'] = $this->convertItemPrototypes($discovery_rule['item_prototypes'],
					$hostname
				);
			}
			if (array_key_exists('trigger_prototypes', $discovery_rule)) {
				foreach ($discovery_rule['trigger_prototypes'] as &$trigger_prototype) {
					$trigger_prototype = $this->convertTrigger($trigger_prototype);
				}
				unset($trigger_prototype);
			}

			$result[] = $discovery_rule;
		}

		return $result;
	}

	/**
	 * Convert item prototypes.
	 *
	 * @param array  $item_prototypes
	 * @param string $hostname
	 *
	 * @return array
	 */
	private function convertItemPrototypes(array $item_prototypes, string $hostname): array {
		$result = [];

		foreach ($item_prototypes as $item_prototype) {
			if (array_key_exists('trigger_prototypes', $item_prototype)) {
				foreach ($item_prototype['trigger_prototypes'] as &$trigger_prototype) {
					$trigger_prototype = $this->convertTrigger($trigger_prototype, $hostname, $item_prototype['key']);
				}
				unset($trigger_prototype);
			}

			$result[] = $item_prototype;
		}

		return $result;
	}

	/**
	 * Convert host prototypes.
	 *
	 * @static
	 *
	 * @param array $host_prototypes
	 *
	 * @return array
	 */
	private static function convertHostPrototypes(array $host_prototypes): array {
		$result = [];

		foreach ($host_prototypes as $host_prototype) {
			if (array_key_exists('interfaces', $host_prototype)) {
				$host_prototype['interfaces'] = self::convertIntefaces($host_prototype['interfaces']);
			}

			$result[] = $host_prototype;
		}

		return $result;
	}

	private static function convertValueMaps(array $import): array {
		$valuemaps = zbx_toHash($import['value_maps'], 'name');
		unset($import['value_maps']);

		if (array_key_exists('hosts', $import)) {
			$import['hosts'] = self::moveValueMaps($import['hosts'], $valuemaps);
		}

		if (array_key_exists('templates', $import)) {
			$import['templates'] = self::moveValueMaps($import['templates'], $valuemaps);
		}

		return $import;
	}

	private static function moveValueMaps(array $hosts, array $valuemaps) {
		foreach ($hosts as &$host) {
			$used_valuemaps = [];

			if (array_key_exists('items', $host)) {
				foreach ($host['items'] as $item) {
					if (array_key_exists('valuemap', $item) && !in_array($item['valuemap']['name'], $used_valuemaps)) {
						if (array_key_exists($item['valuemap']['name'], $valuemaps)) {
							$host['valuemaps'][] = $valuemaps[$item['valuemap']['name']];
							$used_valuemaps[] = $item['valuemap']['name'];
						}
					}
				}
			}

			if (array_key_exists('discovery_rules', $host)) {
				foreach ($host['discovery_rules'] as $drule) {
					if (!array_key_exists('item_prototypes', $drule)) {
						continue;
					}

					foreach ($drule['item_prototypes'] as $item_prototype) {
						if (array_key_exists('valuemap', $item_prototype)
								&& !in_array($item_prototype['valuemap']['name'], $used_valuemaps)) {
							if (array_key_exists($item_prototype['valuemap']['name'], $valuemaps)) {
								$host['valuemaps'][] = $valuemaps[$item_prototype['valuemap']['name']];
								$used_valuemaps[] = $item_prototype['valuemap']['name'];
							}
						}
					}
				}
			}
		}
		unset($host);

		return $hosts;
	}

	/**
	 * Convert trigger expression to new syntax.
	 *
	 * @param array  $trigger
	 * @param string $host     (optional)
	 * @param string $item     (optional)
	 *
	 * @return array
	 */
	private function convertTrigger(array $trigger, ?string $host = null, ?string $item = null): array {
		$converted_expressions = $this->trigger_expression_converter->convert(array_filter([
			'expression' => $trigger['expression'],
			'recovery_expression' => array_key_exists('recovery_expression', $trigger)
				? $trigger['recovery_expression']
				: null,
			'host' => $host,
			'item' => $item
		]));

		foreach (['expression', 'recovery_expression'] as $source) {
			if (array_key_exists($source, $converted_expressions)) {
				$trigger[$source] = $converted_expressions[$source];
			}
		}

		return $trigger;
	}
}
