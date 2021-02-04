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
 * Class for creating sortable lists of items.
 */
class CSortable extends CTag {
	/**
	 * Drag action triggering class for item child elements. If not specified, the whole item will work as drag handle.
	 */
	public const ZBX_STYLE_SORTABLE_DRAG_HANDLE = 'csortable-drag-handle';

	private const ZBX_STYLE_SORTABLE = 'csortable';
	private const ZBX_STYLE_SORTABLE_LIST = 'csortable-list';
	private const ZBX_STYLE_SORTABLE_ITEM = 'csortable-item';

	/**
	 * List of items.
	 *
	 * @var array
	 */
	private $list = [];

	public function __construct(array $list = []) {
		parent::__construct('div', true);

		$this->addClass(self::ZBX_STYLE_SORTABLE);

		foreach ($list as $item) {
			$this->add($item);
		}
	}

	/**
	 * Add item to the list.
	 *
	 * @param mixed $item
	 *
	 * @return CSortable
	 */
	public function add($item): self {
		$this->list[] = $item;

		return $this;
	}

	public function toString($destroy = true) {
		$list = (new CTag('ul', true))->addClass(self::ZBX_STYLE_SORTABLE_LIST);

		foreach ($this->list as $item) {
			$list->addItem((new CTag('li', true, $item))->addClass(self::ZBX_STYLE_SORTABLE_ITEM));
		}

		$this->addItem($list);

		return parent::toString($destroy);
	}
}
