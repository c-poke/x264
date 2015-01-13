import os
import json
import struct
import socket
import random
import string
import psutil
import logging
import requests
import xmltodict
import traceback
import subprocess

from CeleryWorkers.settings import TOKEN
from CeleryWorkers.settings import URL_GET_FILE_HTTP_LINK
from CeleryWorkers.settings import SPLIT_MACHINE_IP
from CeleryWorkers.settings import KEY_SPLIT_MACHINE
from CeleryWorkers.settings import MUX_MACHINE_IP
from CeleryWorkers.settings import KEY_MUX_MACHINE
from CeleryWorkers.settings import WORKER_USERNAME


def get_log(logger='CeleryWorkers'):
    return logging.getLogger(logger)


def generate_string(size=6, chars=string.ascii_uppercase + string.digits + string.ascii_lowercase):
    return ''.join(random.choice(chars) for _ in range(size))


def get_ip_address(ifname):
    try:
        import fcntl
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        return socket.inet_ntoa(fcntl.ioctl(s.fileno(), 0x8915, struct.pack('256s', ifname[:15]))[20:24])
    except ImportError as e:
        get_log().exception(e)
    return None


def get_mac_address(ifname):
    try:
        import fcntl
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        info = fcntl.ioctl(s.fileno(), 0x8927,  struct.pack('256s', ifname[:15]))
        return ''.join(['%02x:' % ord(char) for char in info[18:24]])[:-1]
    except ImportError as e:
        get_log().exception(e)
    return None


def get_ip_mac():
    ip = socket.gethostbyname(socket.gethostname())
    if ip.startswith("127.") and os.name != "nt":
        interfaces = ["eth0", "eth1", "eth2", "wlan0", "wlan1", "wifi0", "ath0", "ath1", "ppp0"]
        for ifname in interfaces:
            ip_address = mac_address = None
            try:
                ip_address = get_ip_address(ifname)
                try:
                    mac_address = get_mac_address(ifname)
                except IOError:
                    mac_address = '00:00:00:00:00:00'
            except IOError:
                continue
            return ip_address, mac_address
    return ip, '00:00:00:00:00:00'


def is_video_file(media_info):
    try:
        for track in media_info['Mediainfo']['File']['track']:
            if track['@type'].lower() == 'video':
                return True
        return False
    except KeyError:
        return False
    except ValueError:
        return False


def run_custom_command(cmnd):
    try:
        if not isinstance(cmnd, list):
            cmnd = cmnd.split()
        p = subprocess.Popen(cmnd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = p.communicate()
        if err:
            get_log().error('Read-header-error: ' + err)
            return err
        if not out:
            get_log().error('Wrong-command-error')
            return None
        return out
    except OSError as e:
        get_log().error('Fatal-error: ' + traceback.format_exc())
        return e.message


def probe_video_file(filename):
    try:
        cmnd = ['ffprobe', '-show_streams', '-show_format', '-loglevel', 'quiet',
                '-print_format', 'json', filename]
        p = subprocess.Popen(cmnd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = p.communicate()
        # error occurs at ffprobe
        if err:
            get_log().error('Read-header-error: ' + err)
            return None
        if not out:
            get_log().error('Wrong-command-error')
            return None
        return out
    except OSError as e:
        get_log().error('Fatal-error: ' + e.message)
        return None


def get_media_info(filename):
    try:
        cmnd = ['mediainfo', '--Output=XML', filename]
        p = subprocess.Popen(cmnd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = p.communicate()
        # error occurs at mediainfo
        if err:
            get_log().error('Read-media-info-error: ' + err)
            return None
        if not out:
            get_log().error('Wrong-command-error')
            return None
        return xmltodict.parse(out)
    except Exception as e:
        get_log().error('Fatal-error: ' + e.message)
        return None


def split_temp(input_path, output_path):
    cmnd = ['ffmpeg', '-i', '<INPUT>', '-c', 'copy', '-t', '60', '-map_metadata', '-1', '-y', '<OUTPUT>']
    sfileName = os.path.splitext(os.path.basename(input_path))[0]
    output_video = output_path + '/' + sfileName + '.mkv'
    for i in range(0, len(cmnd)):
        if cmnd[i] == '<INPUT>':
            cmnd[i] = input_path
        elif cmnd[i] == '<OUTPUT>':
            cmnd[i] = output_video
    p = subprocess.Popen(cmnd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    info, error = p.communicate()
    if error:
        get_log().error('Read-media-info-error: ' + error)
        return None
    if not info:
        get_log().error('Wrong-command-error')
        return None
    return output_video


def get_file_http_link(phys_link, access_id=None, access_password=None):
    request_data = {
        'token': TOKEN,
        'file_path': phys_link,
        'access_id': access_id,
        'access_password': access_password
    }
    headers = {'Content-type': 'application/json; charset=utf-8'}
    response = requests.post(URL_GET_FILE_HTTP_LINK, data=json.dumps(request_data), headers=headers)
    resp_data = response.json()
    if str(resp_data['returnCode']) == '1':
        return resp_data['data']
    return None


def get_device_info():
    info = dict()
    info['num_core'] = psutil.cpu_count()
    try:
        info['total_memory'] = psutil.phymem_usage().total
        info['remaining_memory'] = psutil.phymem_usage().free
        info['volume_size'] = psutil.disk_usage('/').total
        info['used_volume_space'] = psutil.disk_usage('/').used
    except OSError:
        pass
    return info


def create_remote_folders(folder_path, key=KEY_MUX_MACHINE):
    cmd = "/usr/bin/ssh -o StrictHostKeyChecking=no -i %s %s@%s 'mkdir -p %s'" % \
          (str(key), str(WORKER_USERNAME), str(MUX_MACHINE_IP), str(folder_path))
    return_code = subprocess.call(cmd, shell=True)
    if return_code != 0:
        raise Exception('Copy-from-split-machine-error: ' + str(cmd))
    get_log().info('Create-remote-folders-info: Run command %s success' % cmd)
    return True


def copy_from_split_machine(remote_location, current_location):
    cmd = "/usr/bin/rsync -rave 'ssh -i %s -o StrictHostKeyChecking=no' %s@%s:%s %s" \
          % (str(KEY_SPLIT_MACHINE), str(WORKER_USERNAME), str(SPLIT_MACHINE_IP),
             str(remote_location), str(current_location))
    return_code = subprocess.call(cmd, shell=True)
    if return_code != 0:
        raise Exception('Copy-from-split-machine-error: ' + cmd)
    get_log().info('Copy-from-split-machine-info: Run command %s success' % cmd)
    return True


def copy_to_mux_machine(current_location, remote_location):
    if not create_remote_folders(remote_location):
        return False
    cmd = "/usr/bin/rsync -rave 'ssh -i %s -o StrictHostKeyChecking=no' %s %s@%s:%s" \
          % (str(KEY_MUX_MACHINE), str(current_location), str(WORKER_USERNAME),
             str(MUX_MACHINE_IP), str(remote_location))
    return_code = subprocess.call(cmd, shell=True)
    if return_code != 0:
        raise Exception('Copy-to-mux-machine-error: ' + cmd)
    get_log().info('Copy-to-mux-machine-info: Run command %s success' % cmd)
    return True