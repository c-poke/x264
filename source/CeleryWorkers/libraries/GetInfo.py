import os

import xmltodict

from CeleryWorkers.libraries import utils


def get_cfg():
    return {'input': '',
            'print_format': None,
            'select_streams': None,
            'show_format': None,
            'show_streams': None,
            'output': ''}


def call_ffprobe():
    cfg = get_cfg()
    runText = ['ffprobe', '-select_streams', 'v', '-show_streams', '-show_format',
               '-loglevel', 'quiet', '-print_format', 'json', cfg['input']]

    info, error = utils.run_command(runText, True, False)
    # read_info = open(cfg['output'],'rb')
    # info = read_info.read()
    if error:
        return {'e': 1, 'Error': 600, 'Description': 'Read File by FFprobe Error'}
    if not info:
        info = {'e': 1, 'Error': 600, 'Description': 'Read File by FFprobe Error'}
    return info
    #cfgfiles = open('param_ffmpeg.cfg','r')
    #print (cfgfiles['[ffmpegx264]'])


def call_mediainfo():
    cfg = get_cfg()
    runText = ['mediainfo', cfg['input'], '--Output=XML']
    # runText += cfg['output']
    info, error = utils.run_command(runText, True, False)
    # read_info = open(cfg['output'],'rb')
    # info = read_info.read()
    if error:
        return {'e': 1, 'Error': 601, 'Description': 'Read File by Mediainfo Error'}
    if not info:
        return {'e': 1, 'Error': 601, 'Description': 'Read File by Mediainfo Error'}
    return xmltodict.parse(info)


def main(argv):
    input = argv[2]
    cfg = get_cfg()
    fileName = os.path.splitext(os.path.basename(input))[0]
    baseFile = os.path.dirname(input)
    fileInfoExtension = ".json"
    output = baseFile + '/' + fileName + fileInfoExtension
    cfg['input'] = input
    cfg['print_format'] = 'json'
    cfg['select_streams'] = True
    cfg['show_format'] = True
    cfg['show_streams'] = True
    cfg['output'] = output
    info_ffprobe = call_ffprobe()
    info_mediainfo = call_mediainfo()
    return info_ffprobe, info_mediainfo