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


class CControllerServiceRuleValidate extends CController {

	private $ts_from;
	private $ts_to;

	protected function checkInput(): bool {
		// TODO: needs to update validation rules
		$fields = [
			'row_index' =>		'required|int32',
			'new_status' =>		'int32',
			'type' =>			'int32',
			'limit_value' =>	'int32',
			'limit_status' =>	'int32'
		];

		$ret = $this->validateInput($fields);

		if (!$ret) {
			$this->setResponse(
				(new CControllerResponseData([
					'main_block' => json_encode(['errors' => getMessages()->toString()])
				]))->disableView()
			);
		}

		return $ret;
	}

	protected function checkPermissions(): bool {
		return $this->checkAccess(CRoleHelper::UI_MONITORING_SERVICES)
			&& $this->checkAccess(CRoleHelper::ACTIONS_MANAGE_SERVICES);
	}

	protected function doAction(): void {
		$data = [
			'row_index' => $this->getInput('row_index'),
			'form' => [
				'new_status' => $this->getInput('new_status', TRIGGER_SEVERITY_NONE),
				'type' => $this->getInput('type', SERVICE_CALC_STATUS_MORE),
				'limit_value' => $this->getInput('limit_value', 0),
				'limit_status' => $this->getInput('limit_status', TRIGGER_SEVERITY_NONE)
			],
			'user' => [
				'debug_mode' => $this->getDebugMode()
			]
		];

		$this->setResponse(new CControllerResponseData($data));
	}
}
