from __future__ import absolute_import

import re
import time
import json
import requests
from celery import Celery
from celery.signals import celeryd_after_setup

from CeleryWorkers import settings
from CeleryWorkers.utilities import get_log
from CeleryWorkers.utilities import get_ip_mac
from CeleryWorkers.utilities import get_device_info


app = Celery('CeleryWorkers', include=['CeleryWorkers.tasks'])
app.config_from_object(settings)


@celeryd_after_setup.connect
def setup_direct_queue(sender, instance, **kwargs):
    # check if this worker listen encode video queue
    default_queue = None
    pattern = re.compile("^encode_video.+queue$")
    for queue_name in instance.app.amqp.queues.keys():
        if pattern.match(queue_name):
            default_queue = queue_name
            break
    if not default_queue:
        return
    device_info = get_device_info()
    # add default queue
    num_core = int(device_info['num_core'])
    if num_core <= 2:
        default_queue = 'encode_video_small_queue'
    elif 5 <= num_core <= 7:
        default_queue = 'encode_video_large'
    elif num_core >= 8:
        default_queue = 'encode_video_xlarge_queue'
    else:
        instance.app.amqp.queues.select_add('encode_video_medium_queue')
    instance.app.amqp.queues.select_add(default_queue)
    # only if this encode video queue
    ip_address, mac_address = get_ip_mac()
    unique_queue_name = "evw-%s" % str(ip_address)
    instance.app.amqp.queues.select_add(unique_queue_name)
    request_data = {
        'token': settings.TOKEN,
        'ip_address': ip_address,
        'mac_address': mac_address,
        'default_queue': default_queue,
        'unique_queue_name': unique_queue_name,
        'action': 'queueing',
        'created_at': time.time()
    }
    request_data = dict(request_data.items() + device_info.items())
    headers = {'Content-type': 'application/json; charset=utf-8'}
    response = requests.post(settings.URL_NOTIFY_QUEUE, data=json.dumps(request_data), headers=headers)
    get_log().info(response)
    print response.json()

if __name__ == '__main__':
    app.start()