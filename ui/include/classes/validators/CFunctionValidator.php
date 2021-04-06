<?php
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


class CFunctionValidator extends CValidator {

	/**
	 * The array containing valid functions and parameters to them.
	 *
	 * Structure: [
	 *   '<function>' => [
	 *     'args' => [
	 *       [
	 *		   'type' => '<parameter_type>',
	 *         'mandat' => 0x00
	 *       ],
	 *       ...
	 *     ],
	 *     'value_types' => [<value_type>, <value_type>, ...]
	 *   ]
	 * ]
	 *
	 * <parameter_type> can be 'query', 'fit', 'mode', 'num_suffix', 'operation', 'percent', 'sec_neg',
	 *                         'sec_num', 'sec_num_zero', 'sec_zero', 'pattern'
	 * <value_type> can be one of ITEM_VALUE_TYPE_*
	 *
	 * @var array
	 */
	private $allowed;

	/**
	 * If set to true, LLD macros can be used inside functions and are properly validated using LLD macro parser.
	 *
	 * @var bool
	 */
	private $lldmacros = true;

	/**
	 * Calculated item formula validation.
	 *
	 * @var bool
	 */
	private $calculated = false;

	public function __construct(array $options = []) {
		/*
		 * CValidator is an abstract class, so no specific functionality should be bound to it. Thus putting
		 * an option "lldmacros" (or class variable $lldmacros) in it, is not preferred. Without it, class
		 * initialization would fail due to __set(). So instead we create a local variable in this extended class
		 * and remove the option "lldmacros" before calling the parent constructor.
		 */
		if (array_key_exists('lldmacros', $options)) {
			$this->lldmacros = $options['lldmacros'];
			unset($options['lldmacros']);
		}
		if (array_key_exists('calculated', $options)) {
			$this->calculated = (bool) $options['calculated'];
			unset($options['calculated']);
		}
		parent::__construct($options);

		$value_types_all = [
			ITEM_VALUE_TYPE_FLOAT => true,
			ITEM_VALUE_TYPE_UINT64 => true,
			ITEM_VALUE_TYPE_STR => true,
			ITEM_VALUE_TYPE_TEXT => true,
			ITEM_VALUE_TYPE_LOG => true
		];
		$value_types_num = [
			ITEM_VALUE_TYPE_FLOAT => true,
			ITEM_VALUE_TYPE_UINT64 => true
		];
		$value_types_char = [
			ITEM_VALUE_TYPE_STR => true,
			ITEM_VALUE_TYPE_TEXT => true,
			ITEM_VALUE_TYPE_LOG => true
		];
		$value_types_log = [
			ITEM_VALUE_TYPE_LOG => true
		];
		$value_types_int = [
			ITEM_VALUE_TYPE_UINT64 => true
		];

		/*
		 * Types of parameters:
		 * - query - /host/key reference;
		 * - scale - sec|#num:time_shift;
		 * - sec_neg
		 * - fit   - can be either empty or one of valid parameters;
		 * - mode  - can be either empty or one of valid parameters;
		 * - nodata_mode
		 * - function
		 * - operation
		 * - pattern
		 * - period
		 * - percent
		 * - num_suffix
		 *
		 * Mandatory property (mandat):
		 * - 0x00 if parameter is optional;
		 * - 0x01 if parameter is mandatory;
		 * - 0x02 if second part in joint paremater is mandatory (0x03 if both first and second are mandatory).
		 */
		$this->allowed = [
			'avg' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'scale', 'mandat' => 0x01]
				],
				'value_types' => $value_types_num
			],
			'count' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'scale', 'mandat' => 0x01],
					['type' => 'operation', 'mandat' => 0x00],
					['type' => 'pattern', 'mandat' => 0x00]
				],
				'value_types' => $value_types_all
			],
			'change' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01]
				],
				'value_types' => $value_types_all
			],
			'date' => [
				'args' => [],
				'value_types' => $value_types_all
			],
			'dayofmonth' => [
				'args' => [],
				'value_types' => $value_types_all
			],
			'dayofweek' => [
				'args' => [],
				'value_types' => $value_types_all
			],
			'find' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'scale', 'mandat' => 0x00],
					['type' => 'function', 'mandat' => 0x00],
					['type' => 'pattern', 'mandat' => 0x00]
				],
				'value_types' => $value_types_all
			],
			'forecast' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'scale', 'mandat' => 0x01],
					['type' => 'sec_neg', 'mandat' => 0x01],
					['type' => 'fit', 'mandat' => 0x00],
					['type' => 'mode', 'mandat' => 0x00]
				],
				'value_types' => $value_types_num
			],
			'fuzzytime' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'sec_zero', 'mandat' => 0x01]
				],
				'value_types' => $value_types_num
			],
			'last' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'scale', 'mandat' => 0x00]
				],
				'value_types' => $value_types_all
			],
			'length' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01]
				],
				'value_types' => $value_types_all
			],
			'logeventid' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'pattern', 'mandat' => 0x00]
				],
				'value_types' => $value_types_log
			],
			'logseverity' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01]
				],
				'value_types' => $value_types_log
			],
			'logsource' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'pattern', 'mandat' => 0x00]
				],
				'value_types' => $value_types_log
			],
			'max' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'scale', 'mandat' => 0x01]
				],
				'value_types' => $value_types_num
			],
			'min' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'scale', 'mandat' => 0x01]
				],
				'value_types' => $value_types_num
			],
			'nodata' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'sec_neg', 'mandat' => 0x01],
					['type' => 'nodata_mode', 'mandat' => 0x00]
				],
				'value_types' => $value_types_all
			],
			'now' => [
				'args' => [],
				'value_types' => $value_types_all
			],
			'percentile' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'scale', 'mandat' => 0x01],
					['type' => 'percent', 'mandat' => 0x01]
				],
				'value_types' => $value_types_num
			],
			'sum' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'scale', 'mandat' => 0x01]
				],
				'value_types' => $value_types_num
			],
			'time' => [
				'args' => [],
				'value_types' => $value_types_all
			],
			'timeleft' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'scale', 'mandat' => 0x01],
					['type' => 'num_suffix', 'mandat' => 0x01],
					['type' => 'fit', 'mandat' => 0x00]
				],
				'value_types' => $value_types_num
			],
			'trendavg' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'period', 'mandat' => 0x03]
				],
				'value_types' => $value_types_num
			],
			'trendcount' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'period', 'mandat' => 0x03]
				],
				'value_types' => $value_types_all
			],
			'trendmax' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'period', 'mandat' => 0x03]
				],
				'value_types' => $value_types_num
			],
			'trendmin' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'period', 'mandat' => 0x03]
				],
				'value_types' => $value_types_num
			],
			'trendsum' => [
				'args' => [
					['type' => 'query', 'mandat' => 0x01],
					['type' => 'period', 'mandat' => 0x03]
				],
				'value_types' => $value_types_num
			]
		];
	}

	/**
	 * Validate trigger function like last(0), time(), etc.
	 *
	 * @param array                  $value
	 * @param CFunctionParserResult  $value['fn']
	 * @param int                    $value['value_type']  Used only to enable some parameters unquoted.
	 *
	 * @return bool
	 */
	public function validate($value) {
		$this->setError('');

		$value_type = array_key_exists('value_type', $value) ? $value['value_type'] : ITEM_VALUE_TYPE_STR;
		$fn = $value['fn'];

		if (!array_key_exists($fn->function, $this->allowed)) {
			$this->setError(_s('Incorrect trigger function "%1$s" provided in expression.', $fn->match).' '.
				_('Unknown function.'));
			return false;
		}

		if (count($this->allowed[$fn->function]['args']) < count($fn->params_raw['parameters'])) {
			$this->setError(_s('Incorrect trigger function "%1$s" provided in expression.', $fn->match).' '.
				_('Invalid number of parameters.'));
			return false;
		}

		$param_labels = [
			_('Invalid first parameter.'),
			_('Invalid second parameter.'),
			_('Invalid third parameter.'),
			_('Invalid fourth parameter.'),
			_('Invalid fifth parameter.')
		];

		foreach ($this->allowed[$fn->function]['args'] as $num => $arg) {
			// Mandatory check.
			if ($arg['mandat'] && !array_key_exists($num, $fn->params_raw['parameters'])) {
				$this->setError(_s('Incorrect trigger function "%1$s" provided in expression.', $fn->match).' '.
					_('Mandatory parameter is missing.'));
				return false;
			}
			elseif (!array_key_exists($num, $fn->params_raw['parameters'])) {
				continue;
			}

			if (!$this->checkQuotes($arg['type'], $value_type, $fn->params_raw['parameters'][$num])) {
				$this->setError(_s('Incorrect trigger function "%1$s" provided in expression.', $fn->match).' '.
					$param_labels[$num]
				);
				return false;
			}

			$parameter_value = $fn->params_raw['parameters'][$num]->getValue();

			if (($arg['mandat'] & 0x02) && strstr($parameter_value, ':') === false) {
				$this->setError(_s('Incorrect trigger function "%1$s" provided in expression.', $fn->match).' '.
					_('Mandatory parameter is missing.'));
				return false;
			}

			if ($arg['mandat'] != 0x00 && !$this->validateParameter($fn->params_raw['parameters'][$num], $arg)) {
				$this->setError(
					_s('Incorrect trigger function "%1$s" provided in expression.', $fn->match).' '.$param_labels[$num]
				);
				return false;
			}
		}

		return true;
	}

	/**
	 * Validate value type.
	 *
	 * @param array                 $value
	 * @param CFunctionParserResult $value['fn']
	 * @param int                   $value['value_type']  To check if function support particular type of values.
	 *
	 * @return bool
	 */
	public function validateValueType(array $value): bool {
		if (!array_key_exists($value['value_type'], $this->allowed[$value['fn']->function]['value_types'])) {
			$this->setError(_s('Incorrect item value type "%1$s" provided for trigger function "%2$s".',
				itemValueTypeString($value['value_type']), $value['fn']->match));
			return false;
		}

		return true;
	}

	/**
	 * Check if parameter is properly quoted.
	 *
	 * @param string         $type
	 * @param int            $value_type
	 * @param CParserResult  $param
	 *
	 * @return bool
	 */
	private function checkQuotes(string $type, int $value_type, CParserResult $parameter): bool {
		if ($parameter->getValue() === '') {
			return true;
		}

		switch ($type) {
			// Mandatory unquoted.
			case 'query':
			case 'scale':
			case 'period':
			case 'sec_neg':
			case 'sec_zero':
				return !self::isQuoted($parameter->getValue(true));

			// Mandatory quoted based on value type.
			case 'pattern':
				$support_unquoted = (ctype_digit((string) $parameter->getValue())
						&& in_array($value_type, [ITEM_VALUE_TYPE_FLOAT, ITEM_VALUE_TYPE_UINT64]));
				if (!$support_unquoted && !self::isQuoted($parameter->getValue(true))) {
					return false;
				}
				break;

			// Optionally quoted.
			case 'percent':
			case 'num_suffix':
				return true;

			// Mandatory quoted.
			default:
				return self::isQuoted($parameter->getValue(true));
		}

		return true;
	}

	/**
	 * Validate trigger function parameter.
	 *
	 * @param mixed  $param
	 * @param array  $arg
	 * @param string $arg['type']
	 * @param string $arg['mandat']
	 *
	 * @return bool
	 */
	private function validateParameter($param, array $arg): bool {
		switch ($arg['type']) {
			case 'query':
				return $this->validateQuery($param->getValue());

			case 'scale':
				return $this->validateScale($param, $arg['mandat']);

			case 'sec_zero':
				return $this->validateSecZero($param->getValue());

			case 'sec_neg':
				return $this->validateSecNeg($param->getValue());

			case 'num_suffix':
				return $this->validateNumSuffix($param->getValue());

			case 'nodata_mode':
				return ($param->getValue() === 'strict' || $param->getValue() === '');

			case 'fit':
				return ($param->getValue() === '' || $this->validateFit($param->getValue()));

			case 'function':
				return $this->validateStringFunction($param->getValue());

			case 'mode':
				return ($param->getValue() === '' || $this->validateMode($param->getValue()));

			case 'percent':
				return $this->validatePercent($param->getValue());

			case 'operation':
				return $this->validateOperation($param->getValue());

			case 'period':
				return $this->validatePeriod($param, $arg['mandat']);
		}

		return true;
	}

	/**
	 * Validate trigger function parameter which can contain host and item key.
	 * Examples: /host/key, /host/vfs.fs.size["/var/log",pfree]
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validateQuery(string $param): bool {
		if ($this->isMacro($param)) {
			return true;
		}

		$parser = new CQueryParser(['calculated' => $this->calculated]);
		return ($parser->parse($param) === CParser::PARSE_SUCCESS);
	}

	/**
	 * Validate joint "sec|#num:time_shift" syntax.
	 *
	 * @param mixed $param
	 *
	 * @return bool
	 */
	private function validateScale($param, int $mandat): bool {
		if ($this->isMacro($param->getValue())) {
			return true;
		}

		if (!($param instanceof CPeriodParserResult)) {
			return false;
		}
		else {
			$period = $param->sec_num;
			$time_shift = $param->time_shift;
		}

		$is_sec_num_valid = ((!($mandat & 0x01) && $period === '')
				|| $param->sec_num_contains_macros
				|| $this->validateSecNum($period));
		$is_time_shift_valid = ((!($mandat & 0x02) && ($time_shift === '' || $time_shift === null))
				|| $param->time_shift_contains_macros
				|| (!($mandat & 0x02) && $time_shift !== null && $this->validatePeriodShift($time_shift)));

		return ($is_sec_num_valid && $is_time_shift_valid);
	}

	/**
	 * Validate joint "period:period_shift" period syntax.
	 *
	 * Valid period can contain time unit not less than 1 hour and multiple of an hour.
	 * Valid period shift can contain time range value with precision and multiple of an hour.
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validatePeriod(string $param, int $mandat): bool {
		if ($this->isMacro($param)) {
			return true;
		}

		if (!($param instanceof CPeriodParserResult)) {
			return false;
		}
		else {
			$period = $param->sec_num;
			$period_shift = $param->time_shift;
		}

		if (($mandat & 0x01) && !$param->sec_num_contains_macros) {
			$simple_interval_parser = new CSimpleIntervalParser(['with_year' => true]);
			if ($simple_interval_parser->parse($period) != CParser::PARSE_SUCCESS) {
				return false;
			}

			$value = timeUnitToSeconds($period, true);
			if ($value < SEC_PER_HOUR || $value % SEC_PER_HOUR != 0) {
				return false;
			}
		}

		if (!($mandat & 0x02) && $period_shift === null) {
			return true;
		}
		elseif (($mandat & 0x02) && !$this->validateTrendPeriods($period, $period_shift)) {
			return $this->isMacro($period_shift);
		}

		return true;
	}

	/**
	 * Validate trigger function parameter seconds value.
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validateSecValue(string $param): bool {
		return ($this->isMacro($param) || preg_match('/^\d+['.ZBX_TIME_SUFFIXES.']{0,1}$/', $param));
	}

	/**
	 * Validate trigger function parameter which can contain only seconds or zero.
	 * Examples: 0, 1, 5w
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validateSecZero(string $param): bool {
		return ($this->isMacro($param) || $this->validateSecValue($param));
	}

	/**
	 * Validate trigger function parameter which can contain negative seconds.
	 * Examples: 0, 1, 5w, -3h
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validateSecNeg(string $param): bool {
		return ($this->isMacro($param) || preg_match('/^[-]?\d+['.ZBX_TIME_SUFFIXES.']{0,1}$/', $param));
	}

	/**
	 * Validate trigger function parameter which can contain seconds greater than zero or count.
	 * Examples: 1, 5w, #1
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validateSecNum(string $param): bool {
		if ($this->isMacro($param)) {
			return true;
		}

		return preg_match('/^#\d+$/', $param)
			? (substr($param, 1) > 0)
			: ($this->validateSecValue($param) && $param >= 0);
	}

	/**
	 * Validate trigger function parameter which can contain suffixed decimal number.
	 * Examples: 0, 1, 5w, -3h, 10.2G
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validateNumSuffix(string $param): bool {
		return ($this->isMacro($param)
			|| (new CNumberParser(['with_suffix' => true]))->parse($param) == CParser::PARSE_SUCCESS);
	}

	/**
	 * Validate trigger function parameter which can contain fit function (linear, polynomialN with 1 <= N <= 6,
	 * exponential, logarithmic, power) or an empty value.
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validateFit(string $param): bool {
		return ($this->isMacro($param)
			|| preg_match('/^(linear|polynomial[1-6]|exponential|logarithmic|power)$/', $param));
	}

	/**
	 * Validate trigger function parameter which can contain forecast mode (value, max, min, delta, avg) or
	 * an empty value.
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validateMode(string $param): bool {
		return ($this->isMacro($param) || preg_match('/^(value|max|min|delta|avg)$/', $param));
	}

	/**
	 * Validate 'find' operator.
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validateStringFunction(string $param): bool {
		if ($this->isMacro($param)) {
			return true;
		}
		else {
			return in_array($param, ['iregexp', 'regexp', 'like']);
		}
	}

	/**
	 * Validate trigger function parameter which can contain a percentage.
	 * Examples: 0, 1, 1.2, 1.2345, 1., .1, 100
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validatePercent(string $param): bool {
		return ($this->isMacro($param) || preg_match('/^\d*(\.\d{0,4})?$/', $param) && $param !== '.' && $param <= 100);
	}

	/**
	 * Validate trigger function parameter which can contain operation (band, eq, ge, gt, le, like, lt, ne,
	 * regexp, iregexp) or an empty value.
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validateOperation(string $param): bool {
		return ($this->isMacro($param) || preg_match('/^(eq|ne|gt|ge|lt|le|like|bitand|regexp|iregexp|)$/', $param));
	}

	/**
	 * Validate trigger function parameter which must contain time range value with precision.
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function validatePeriodShift(string $param): bool {
		$relative_time_parser = new CRelativeTimeParser();

		return ($relative_time_parser->parse($param) == CParser::PARSE_SUCCESS);
	}

	/**
	 * Validate trend* function used period and period_shift arguments.
	 *
	 * @param string $period_value        Value of period.
	 * @param string $period_shift_value  Value of period shift.
	 *
	 * @return bool
	 */
	private function validateTrendPeriods(string $period_value, string $period_shift_value): bool {
		$precisions = 'hdwMy';
		$period = strpos($precisions, substr($period_value, -1));

		if ($period !== false) {
			if (substr($period_shift_value, 0, 4) !== 'now/') {
				return false;
			}
			$period_shift_value = substr($period_shift_value, 4);

			$relative_time_parser = new CRelativeTimeParser();
			if ($relative_time_parser->parse($period_shift_value) !== CParser::PARSE_SUCCESS) {
				return false;
			}

			foreach ($relative_time_parser->getTokens() as $token) {
				if ($token['type'] !== CRelativeTimeParser::ZBX_TOKEN_PRECISION) {
					continue;
				}

				if (strpos($precisions, $token['suffix']) < $period) {
					$this->setError(_('Time units in period shift must be greater or equal to period time unit.'));
					return false;
				}
			}
		}

		return true;
	}

	/**
	 * Check if parameter is valid macro.
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function isMacro(string $param): bool {
		$user_macro_parser = new CUserMacroParser();
		if ($this->lldmacros) {
			$lld_macro_parser = new CLLDMacroParser();
			$lld_macro_function_parser = new CLLDMacroFunctionParser();
		}

		$is_valid_lld_macro = ($this->lldmacros
			&& ($lld_macro_function_parser->parse($param) == CParser::PARSE_SUCCESS
				|| $lld_macro_parser->parse($param) == CParser::PARSE_SUCCESS)
		);

		return ($user_macro_parser->parse($param) == CParser::PARSE_SUCCESS || $is_valid_lld_macro);
	}

	/**
	 * Check if given value is properly quoted.
	 *
	 * @param string $param
	 *
	 * @return bool
	 */
	private function isQuoted(string $param): bool {
		return (substr($param, 0, 1) === '"' && substr($param, -1) === '"' && substr($param, -2) !== '\"');
	}
}
