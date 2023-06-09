<?php declare(strict_types = 0);

namespace Modules\Example_A\Actions;

use CController as CAction;

class Test extends CAction {

	public function init() {
		$this->disableCsrfValidation();
	}

	/**
	 * @inheritDoc
	 */
	protected function checkPermissions() {
		return true;
	}

	/**
	 * @inheritDoc
	 */
	protected function checkInput() {
		return true;
	}

	/**
	 * @inheritDoc
	 */
	protected function doAction() {
		$response = new \CControllerResponseData([]);
		$response->setTitle('1st Module');

		$this->setResponse($response);
	}
}
