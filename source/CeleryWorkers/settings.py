import os

import local
IS_LOCAL = False

BASE_APP_DIR = os.path.dirname(__file__)

KEYS_DIR = os.path.join(BASE_APP_DIR, 'keys')

SPLIT_MACHINE_IP = '10.0.0.7' if not IS_LOCAL else local.SPLIT_MACHINE_IP

KEY_SPLIT_MACHINE = os.path.join(KEYS_DIR, 'transcode_user.key') if not IS_LOCAL else local.KEY_SPLIT_MACHINE

MUX_MACHINE_IP = '10.0.0.6' if not IS_LOCAL else local.MUX_MACHINE_IP

KEY_MUX_MACHINE = os.path.join(KEYS_DIR, 'transcode_user.key') if not IS_LOCAL else local.KEY_MUX_MACHINE

WORKER_USERNAME = 'transcoding_user' if not IS_LOCAL else local.WORKER_USERNAME

# API configurations
TRANSCODING_SERVICE_URL = 'http://api-transcode.hdviet.com/backend/' if not IS_LOCAL else local.TRANSCODING_SERVICE_URL

URL_NOTIFY_QUEUE = TRANSCODING_SERVICE_URL + 'worker/notify'

URL_NOTIFY = TRANSCODING_SERVICE_URL + 'job/notify'
URL_NOTIFY_SPLIT = TRANSCODING_SERVICE_URL + 'job/split'
URL_NOTIFY_JOIN = TRANSCODING_SERVICE_URL + 'job/join'
URL_NOTIFY_INFO = TRANSCODING_SERVICE_URL + 'job/info'

FILE_SERVICE_URL = 'http://10.0.0.6:7000/api/' if not IS_LOCAL else local.FILE_SERVICE_URL

URL_GET_FILE_HTTP_LINK = FILE_SERVICE_URL + 'file/getlink'

TOKEN = 'HMUDzrmQMhv8HJKPWRczQqQr85bSuBBXhgNMxe9VxVW7SgrVSz'

# for split worker
INPUT_SEGMENT_FOLDER = '/data/process' if not IS_LOCAL else local.INPUT_SEGMENT_FOLDER

# for encode video worker, encode video worker, muxing worker
TEMP_ENCODE_AUDIO = os.path.join('/tmp', 'encode_audio')
TEMP_ENCODE_VIDEO = os.path.join('/tmp', 'encode_video')

OUTPUT_SEGMENT_FOLDER = '/data/output' if not IS_LOCAL else local.OUTPUT_SEGMENT_FOLDER
OUTPUT_ENCODE_AUDIO = os.path.join(OUTPUT_SEGMENT_FOLDER, 'encode_audio')
OUTPUT_ENCODE_VIDEO = os.path.join(OUTPUT_SEGMENT_FOLDER, 'encode_video')
OUTPUT_MUXING = os.path.join(OUTPUT_SEGMENT_FOLDER, 'muxing')


# Celery configurations
BROKER_URL = 'amqp://to_daemon_rabbit_user:GxWHg0TLvzHuFIzv6tGW@10.0.0.5:5672//TOENV' \
    if not IS_LOCAL else local.BROKER_URL

# CELERY_RESULT_DBURI = 'mysql://to_remote_user:7610Zp18M4t4Gqk2J2ur@10.0.0.5:3306/transcoding_service' \
#     if not IS_LOCAL else local.CELERY_RESULT_DBURI

CELERY_ROUTES = {
    'CeleryWorkers.tasks.get_file_info': {
        'queue': 'file_info_queue'
    },
    'CeleryWorkers.tasks.split_video': {
        'queue': 'split_video_queue'
    },
    'CeleryWorkers.tasks.encode_video': {
        'queue': 'encode_video_queue'
    },
    'CeleryWorkers.tasks.encode_audio': {
        'queue': 'encode_audio_queue'
    },
    'CeleryWorkers.tasks.muxing': {
        'queue': 'muxing_queue'
    },
}

CELERY_CREATE_MISSING_QUEUES = True

CELERYD_PREFETCH_MULTIPLIER = 1

CELERY_ENABLE_UTC = True

CELERY_TIMEZONE = 'Asia/Ho_Chi_Minh'

# CELERY_RESULT_BACKEND = "database"

CELERY_RESULT_SERIALIZER = 'json'

CELERY_TASK_RESULT_EXPIRES = 60*60*24*30

BROKER_HEARTBEAT = 60

BROKER_HEARTBEAT_CHECKRATE = 2

BROKER_CONNECTION_MAX_RETRIES = None

CELERY_TRACK_STARTED = True

CELERYMON_LOG_FORMAT = "%(levelname)s\t%(asctime)s\t%(filename)s\t%(lineno)d\t%(message)s"
