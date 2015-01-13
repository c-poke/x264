from __future__ import absolute_import

import sys
import time
import shutil

from hashlib import md5
from celery import shared_task
from celery.utils.log import get_task_logger

from CeleryWorkers.settings import INPUT_SEGMENT_FOLDER
from CeleryWorkers.settings import TEMP_ENCODE_AUDIO
from CeleryWorkers.settings import TEMP_ENCODE_VIDEO
from CeleryWorkers.settings import OUTPUT_ENCODE_AUDIO
from CeleryWorkers.settings import OUTPUT_ENCODE_VIDEO
from CeleryWorkers.settings import OUTPUT_MUXING
from CeleryWorkers.base_task import BaseTask
from CeleryWorkers.utilities import *
from CeleryWorkers.libraries import SplitVideo
from CeleryWorkers.libraries import EncodeAudio
from CeleryWorkers.libraries import EncodeVideo
from CeleryWorkers.libraries import Join
from CeleryWorkers.libraries.LibraryEncode import check_input


MAX_RETRY_TIMES = 1
DELAY_TIME_IN_SECONDS = 60
logger = get_task_logger('CeleryWorkers')


@shared_task(bind=True, base=BaseTask)
def get_file_info(self, data):
    # add log
    old_outs = sys.stdout, sys.stderr
    read_level = self.app.conf.CELERY_REDIRECT_STDOUTS_LEVEL
    # custom field
    temp_folder = None
    try:
        self.app.log.redirect_stdouts_to_logger(logger, read_level)
        data = json.loads(data)
        ##################################################################################
        # Start worker
        ##################################################################################
        # generate sub folder
        output_sub_folder = os.path.join(INPUT_SEGMENT_FOLDER, time.strftime("%Y%m"))
        # generate output folder
        input_link = get_file_http_link(data['input_source'], data['access_id'], data['access_password'])
        hasher = md5()
        hasher.update(input_link)
        hasher.update(str(time.time()))
        hasher.update('7702g)58zK9y!8C')
        temp_folder = os.path.join(output_sub_folder, hasher.hexdigest()[:10])
        # create folder
        if os.path.isfile(temp_folder) is True:
            os.remove(temp_folder)
        if os.path.isdir(temp_folder) is False:
            os.makedirs(temp_folder, 0775)
        # copy file to temp
        temp_file = split_temp(input_link, temp_folder)
        # get file info
        result = {}
        mediainfo_data = json.loads(json.dumps(get_media_info(temp_file)))['Mediainfo']
        ffprobe_data = json.loads(probe_video_file(temp_file))
        final_info = check_input(ffprobe_data, mediainfo_data)
        if not final_info:
            raise Exception('This format is not support')
        result['info'] = final_info
        result['duration'] = float(final_info['Video'][0]['Duration'])
        return result
        ##################################################################################
        # End worker
        ##################################################################################
    finally:
        try:
            if temp_folder is not None:
                if os.path.isfile(temp_folder) is True:
                    os.remove(temp_folder)
                if os.path.isdir(temp_folder) is True:
                    shutil.rmtree(temp_folder)
        finally:
            sys.stdout, sys.stderr = old_outs


@shared_task(bind=True, base=BaseTask)
def split_video(self, data):
    # add log
    old_outs = sys.stdout, sys.stderr
    read_level = self.app.conf.CELERY_REDIRECT_STDOUTS_LEVEL
    try:
        self.app.log.redirect_stdouts_to_logger(logger, read_level)
        data = json.loads(data)
        ##################################################################################
        # Start worker
        ##################################################################################
        # generate sub folder
        output_sub_folder = os.path.join(INPUT_SEGMENT_FOLDER, time.strftime("%Y%m"))
        # generate output folder
        input_link = get_file_http_link(data['input_source'], data['access_id'], data['access_password'])
        hasher = md5()
        hasher.update(input_link)
        hasher.update(str(time.time()))
        hasher.update('8c\\0pyQb>91z#9g')
        output_folder = os.path.join(output_sub_folder, hasher.hexdigest()[:10])
        # create folder
        if os.path.isfile(output_folder) is True:
            os.remove(output_folder)
        if os.path.isdir(output_folder) is False:
            os.makedirs(output_folder, 0775)
        # splitting file
        result = SplitVideo.split_file(input_link, output_folder, data['command'])
        if len(result) > 0:
            for file_path in result:
                os.chmod(file_path, 0777)
            return result
        raise Exception('Unknown error: failed to split file')
        ##################################################################################
        # End worker
        ##################################################################################
    finally:
        sys.stdout, sys.stderr = old_outs


@shared_task(bind=True, base=BaseTask)
def encode_video(self, data):
    # add log
    old_outs = sys.stdout, sys.stderr
    read_level = self.app.conf.CELERY_REDIRECT_STDOUTS_LEVEL
    temp_folder = None
    try:
        self.app.log.redirect_stdouts_to_logger(logger, read_level)
        data = json.loads(data)
        ##################################################################################
        # Start worker
        ##################################################################################
        # generate sub folder
        output_sub_folder = os.path.join(OUTPUT_ENCODE_VIDEO, time.strftime("%Y%m"))
        input_path = data["piece_data"]["input_path"]
        # generate output folder
        hasher = md5()
        hasher.update(input_path)
        hasher.update(str(time.time()))
        hasher.update('@N)|j!@1#c4^x=i')
        folder_name = hasher.hexdigest()[:10]
        output_folder = os.path.join(output_sub_folder, folder_name)
        temp_folder = os.path.join(TEMP_ENCODE_VIDEO, folder_name)
        temp_process_folder = os.path.join(temp_folder, 'output')
        # create folder
        if os.path.isfile(temp_folder) is True:
            os.remove(temp_folder)
        if os.path.isdir(temp_folder) is False:
            os.makedirs(temp_folder, 0775)
        if os.path.isfile(temp_process_folder) is True:
            os.remove(temp_process_folder)
        if os.path.isdir(temp_process_folder) is False:
            os.makedirs(temp_process_folder, 0775)
        # copy piece to local disk
        if copy_from_split_machine(input_path, temp_folder):
            # process
            temp_file = os.path.join(temp_folder, os.path.basename(input_path))
            video = EncodeVideo.main(temp_file, temp_process_folder, data['command'])
        else:
            raise Exception("Cannot rsync from %s to %s" % (input_path, temp_folder))
        # copy piece to mux machine
        if video["e"] == 0:
            if copy_to_mux_machine(video["Data"]["Output"], output_folder):
                return os.path.join(output_folder, os.path.basename(video["Data"]["Output"]))
            raise Exception("Video: cannot rsync from %s to %s" % (video["Data"]["Output"], output_folder))
        raise Exception(video["Description"])
        ##################################################################################
        # End worker
        ##################################################################################
    finally:
        sys.stdout, sys.stderr = old_outs
        if temp_folder:
            if os.path.isfile(temp_folder):
                os.remove(temp_folder)
            elif os.path.isdir(temp_folder):
                shutil.rmtree(temp_folder)


@shared_task(bind=True, base=BaseTask)
def encode_audio(self, data):
    # add log
    old_outs = sys.stdout, sys.stderr
    read_level = self.app.conf.CELERY_REDIRECT_STDOUTS_LEVEL
    temp_folder = None
    try:
        self.app.log.redirect_stdouts_to_logger(logger, read_level)
        data = json.loads(data)
        ##################################################################################
        # Start worker
        ##################################################################################
        # generate sub folder
        output_sub_folder = os.path.join(OUTPUT_ENCODE_AUDIO, time.strftime("%Y%m"))
        # generate output folder
        input_link = get_file_http_link(data['input_source'], data['access_id'], data['access_password'])
        hasher = md5()
        hasher.update(input_link)
        hasher.update(str(time.time()))
        hasher.update('@N)|j!@1#c4^x=i')
        folder_name = hasher.hexdigest()[:10]
        output_folder = os.path.join(output_sub_folder, folder_name)
        temp_folder = os.path.join(TEMP_ENCODE_AUDIO, folder_name)
        # create folder
        if os.path.isfile(temp_folder) is True:
            os.remove(temp_folder)
        if os.path.isdir(temp_folder) is False:
            os.makedirs(temp_folder, 0775)
        # start job
        audio = EncodeAudio.main(input_link, data['audio_type'], temp_folder, data['command'])
        if audio["e"] == 0:
            if copy_to_mux_machine(audio["Data"]["Output"], output_folder):
                return os.path.join(output_folder, os.path.basename(audio["Data"]["Output"]))
            raise Exception("Audio: cannot rsync from %s to %s" % (audio["Data"]["Output"], output_folder))
        raise Exception(audio["Description"])
        ##################################################################################
        # End worker
        ##################################################################################
    finally:
        sys.stdout, sys.stderr = old_outs
        if temp_folder:
            if os.path.isfile(temp_folder):
                os.remove(temp_folder)
            elif os.path.isdir(temp_folder):
                shutil.rmtree(temp_folder)


@shared_task(bind=True, base=BaseTask)
def muxing(self, data):
    # add log
    old_outs = sys.stdout, sys.stderr
    read_level = self.app.conf.CELERY_REDIRECT_STDOUTS_LEVEL
    try:
        self.app.log.redirect_stdouts_to_logger(logger, read_level)
        data = json.loads(data)
        ##################################################################################
        # Start worker
        ##################################################################################
        # generate sub folder
        output_sub_folder = os.path.join(OUTPUT_MUXING, time.strftime("%Y%m"))
        # generate output folder
        hasher = md5()
        hasher.update(str(time.time()))
        hasher.update('@N)|j!@1#c4^x=i')
        output_folder = os.path.join(output_sub_folder, hasher.hexdigest()[:10])
        # create folder
        if os.path.isfile(output_folder) is True:
            os.remove(output_folder)
        if os.path.isdir(output_folder) is False:
            os.makedirs(output_folder, 0775)
        # start job
        videoInputs = ""
        audioInput = ""
        for piece_data in data["piece_list"]:
            # inputs += piece_data["output_path"] + ","
            if piece_data["type_data"] == "audio":
                audioInput = piece_data["output_path"]
            else:
                videoInputs += piece_data["output_path"] + ","
        if len(videoInputs) > 0:
            videoInputs = videoInputs[:-1]
        tempFolder = os.path.dirname(audioInput)
        """ template get from db """
        template = ['MP4Box', '-flat', '-force-cat']
        join = Join.main(
            videoInputs,
            audioInput,
            tempFolder,
            output_folder + "/" + generate_string(8) + ".mp4",
            template)
        # remove process file
        if join["e"] == 0:
            os.chmod(join["Data"]["Output"], 0775)
            return join["Data"]["Output"]
        raise Exception(join["Description"])
        ##################################################################################
        # End worker
        ##################################################################################
    finally:
        sys.stdout, sys.stderr = old_outs


@shared_task(bind=True)
def run_command(self, data):
    # add log
    old_outs = sys.stdout, sys.stderr
    read_level = self.app.conf.CELERY_REDIRECT_STDOUTS_LEVEL
    try:
        self.app.log.redirect_stdouts_to_logger(logger, read_level)
        data = json.loads(data)
        ##################################################################################
        # Start worker
        ##################################################################################
        message = run_custom_command(data['command'])
        rest_time = data['rest_time'] if 'rest_time' in data else 60
        time.sleep(rest_time)
        return message
        ##################################################################################
        # End worker
        ##################################################################################
    finally:
        sys.stdout, sys.stderr = old_outs