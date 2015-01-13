from __future__ import absolute_import

import json
import requests
from celery import Task

from CeleryWorkers.settings import TOKEN
from CeleryWorkers.settings import URL_NOTIFY
from CeleryWorkers.settings import URL_NOTIFY_JOIN
from CeleryWorkers.settings import URL_NOTIFY_SPLIT
from CeleryWorkers.settings import URL_NOTIFY_INFO


class BaseTask(Task):
    abstract = True

    logger = None
    url_api = None
    job_id = None
    file_id = None
    piece_id = None

    def on_success(self, retval, task_id, args, kwargs):
        data = json.loads(args[0])
        self.job_id = get_value(data, 'job_id')
        self.file_id = get_value(data, 'file_id')
        try:
            self.piece_id = data['piece_data']['id']
        except KeyError:
            pass
        # set URL API and request data
        request_data = None
        if self.name == 'CeleryWorkers.tasks.encode_video' or self.name == 'CeleryWorkers.tasks.encode_audio':
            self.url_api = URL_NOTIFY
            request_data = {
                'token': TOKEN,
                'job_id': self.job_id,
                'piece_id': self.piece_id,
                'output_path': retval,
                'action': 'success'}
        elif self.name == 'CeleryWorkers.tasks.muxing':
            self.url_api = URL_NOTIFY_JOIN
            request_data = {
                'token': TOKEN,
                'job_id': self.job_id,
                'output_path': retval,
                'action': 'success'}
        elif self.name == 'CeleryWorkers.tasks.split_video':
            self.url_api = URL_NOTIFY_SPLIT
            request_data = {
                'token': TOKEN,
                'job_id': self.job_id,
                'output_paths': retval,
                'action': 'success'}
        elif self.name == 'CeleryWorkers.tasks.get_file_info':
            self.url_api = URL_NOTIFY_INFO
            request_data = {
                'token': TOKEN,
                'file_id': self.file_id,
                'info': retval['info'],
                'duration': retval['duration'],
                'action': 'success'}
        # begin process
        if self.url_api is None or request_data is None:
            print "Bad task name: " + str(self.name)

        headers = {'Content-type': 'application/json; charset=utf-8'}
        response = requests.post(self.url_api, data=json.dumps(request_data), headers=headers)
        print response.json()
        # call parent method
        super(BaseTask, self).on_success(retval, task_id, args, kwargs)

    def on_failure(self, exc, task_id, args, kwargs, einfo):
        data = json.loads(args[0])
        self.job_id = get_value(data, 'job_id')
        self.file_id = get_value(data, 'file_id')
        try:
            self.piece_id = data['piece_data']['id']
        except KeyError:
            pass
        if self.name == 'CeleryWorkers.tasks.encode_video' or self.name == 'CeleryWorkers.tasks.encode_audio':
            self.url_api = URL_NOTIFY
        elif self.name == 'CeleryWorkers.tasks.muxing':
            self.url_api = URL_NOTIFY_JOIN
        elif self.name == 'CeleryWorkers.tasks.split_video':
            self.url_api = URL_NOTIFY_SPLIT
        elif self.name == 'CeleryWorkers.tasks.get_file_info':
            self.url_api = URL_NOTIFY_INFO
        # begin process
        if self.url_api is None:
            print "Bad task name: " + str(self.name)
        request_data = {
            'token': TOKEN,
            'job_id': self.job_id,
            'piece_id': self.piece_id,
            'description': einfo.traceback,
            'action': 'failed'
        }
        headers = {'Content-type': 'application/json; charset=utf-8'}
        response = requests.post(self.url_api, data=json.dumps(request_data), headers=headers)
        print response.json()
        # call parent method
        super(BaseTask, self).on_failure(exc, task_id, args, kwargs, einfo)


def get_value(data_set, key_name):
    if not isinstance(data_set, dict) or not isinstance(key_name, str):
        return None
    try:
        return data_set[key_name]
    except KeyError:
        pass
    return None