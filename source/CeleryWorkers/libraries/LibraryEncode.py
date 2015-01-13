__author__ = 'linhtd'

import subprocess
import os
import json
import re
import ast
import math

import xmltodict


def code_name():
    return ['h264', 'vc1', 'mpeg2video', 'hevc', 'mpeg4', 'flv', 'flv1', 'mpeg1video', 'vp8', 'vp9', 'wmv1', 'wmv2',
            'wmv3',
            'rawvideo', 'rv10', 'rv20', 'rv30', 'rv40', 'mts2', 'mvc1', 'mvc2']


def cmnd_ffprobe():
    return ['ffprobe', '-show_streams', '-show_format',
            '-loglevel', 'quiet', '-print_format', 'json', '/store/SourceMKV/sample_2Au2Def_AC3_FID_FHD.mkv']


def cmnd_Mediainfo():
    return ['mediainfo', '/store/Tool_Transcode_HDViet/0Trancode_new/2video.mkv', '--Output=XML']


def code_framerate():
    return ['23.976','24','25','29.970','30','50','60']


def call_ffprobe(cmnd):
    return call_tool(cmnd)


def call_mediainfo(cmnd):
    return xmltodict.parse(call_tool(cmnd))


def check_code_name(CodeName,info_code_name):
    code = info_code_name
    if CodeName in code:
        return True
    return False


def get_Info_Audio(audio_stream):
    try:
        streams_audio_default = audio_stream['index']
        channels = audio_stream['channels']
        if 'channels' in audio_stream:
            if audio_stream['channels'] >= 6:
                audio_type = 'ac3'
            else:
                audio_type = 'aac'
        elif 'Channel_s_' in audio_stream:
            audio_stream_temp = audio_stream['Channel_s_'].split(' ')
            if int(audio_stream_temp[0]) >= 6:
                audio_type = 'ac3'
            else:
                audio_type = 'aac'
        else:
            audio_type = 'aac'
        bitrate_source_audio = '64K'
        if 'bit_rate' in audio_stream:
            bitrate_source_audio = str(int(audio_stream['bit_rate']) / 1000)
        elif 'sample_rate' in audio_stream:
            bitrate_source_audio = str(int(audio_stream['sample_rate']) / 1000)
        result = {
            'Stream': streams_audio_default,
            'Type': audio_type,
            'Channel': channels,
            'Bitrate': bitrate_source_audio
        }
        return result
    except:
        return None


#return:[{'Bitrate': '448K', 'Type': 'ac3', 'Channel': 6, 'Stream': 0}, {'Bitrate': '640K', 'Type': 'ac3', 'Channel': 6, 'Stream': 2}]
def check_audio(Audio):
    if not Audio:
        return None
    Audio_ = []
    for stream_audio in Audio:
        temp = get_Info_Audio(stream_audio)
        if temp:
            Audio_.append(temp)
    return Audio_


def get_info_video_from_ffprobe(video_stream, info_code_name):
    try:
        stream_video = str(video_stream['index'])
        codec_name = video_stream['codec_name']
        if not check_code_name(codec_name,info_code_name) is True:
            return None
        width_source = video_stream['width']
        height_source = video_stream['height']
        # r_frame_rate = video_stream['r_frame_rate']
        result = {
            'Stream': stream_video,
            'Width': width_source,
            'Height': height_source
        }
        return result
    except:
        return None


def check_video_from_ffprobe(Video, info_code_name):
    if not Video:
        return None
    Video_ = []
    for stream_video in Video:
        temp = get_info_video_from_ffprobe(stream_video, info_code_name)
        Video_.append(temp)
    return Video_
    # try:
    #     Video = json.dumps(Video)
    #     temp = ast.literal_eval(Video)
    #     stream_video = temp[0]['index']
    #     codec_name = temp[0]['codec_name']
    #     if not check_code_name(codec_name) is True:
    #         return None
    #     width = temp[0]['width']
    #     height = temp[0]['height']
    #     display_aspect_ratio = temp[0]['display_aspect_ratio']
    #     r_frame_rate = temp[0]['r_frame_rate']
    #     result = {
    #         'Stream': stream_video,
    #         'Width': width,
    #         'Height': height,
    #         'Ratio': display_aspect_ratio,
    #         'Framerate': r_frame_rate
    #     }
    #     return result
    # except:
    #     return None


def get_info_video_from_mediainfo(video_stream, info_framerate, bitrate_video,duration_video):
    try:
        stream_ = get_stream(video_stream)
        interlaced_ = get_interlaced(video_stream)
        framerate_ = get_framerate(video_stream, info_framerate)
        duration_ = get_duration(video_stream)
        bitrate_ = get_bitrate(video_stream)
        output = {
            'Stream' : stream_,
            'Interlaced' : interlaced_,
            'Framerate' : framerate_,
            'Duration' : duration_video,
            'Bitrate' : bitrate_video
        }
        return output
    except:
        return None


def check_video_from_mediainfo(video_mediainfo, info_framerate, bitrate_video,duration_video):
    if not video_mediainfo:
        return None
    Video__ = []
    info_mediainfo = json.dumps(video_mediainfo)
    #info_mediainfo = ast.literal_eval(info_mediainfo)
    info_mediainfo = json.loads(info_mediainfo)
    for streams_media in info_mediainfo['File']['track']:
        if streams_media['@type'] == 'Video':
            temp = get_info_video_from_mediainfo(streams_media, info_framerate, bitrate_video,duration_video)
            Video__.append(temp)
    return Video__


def get_interlaced(streams_media):
    interlaced = 'False'
    if 'Scan_order' in streams_media or streams_media['Scan_type'] == 'Interlaced':
        interlaced = 'True'
    return interlaced


def set_framerate(fr,info_framerate):
    for ori_fr in info_framerate:
        if fr[0:2] == ori_fr[0:2]:
            return ori_fr
    return None


def get_framerate(streams_media,info_framerate):
    if 'Frame_rate' in streams_media:
        fr = str(streams_media['Frame_rate']).replace(" fps","")
        if fr in info_framerate:
            return fr
        else:
            return set_framerate(fr,info_framerate)
    return None


def get_stream(streams_media):
    if '@streamid' in streams_media:
        return str(int(streams_media['@streamid']) - 1)
    if 'ID' in streams_media:
        return str(int(streams_media['ID']) - 1)
    return None


def get_duration(streams_media):
    if 'Duration' in streams_media:
        dur = str(streams_media['Duration'])
        return dur
    return None


def get_bitrate(streams_media):
    if 'Bit_rate' in streams_media:
        return streams_media['Bit_rate']
    return None


def process_info_video(info_video_mediainfo,info_video_ffprobe,info_code_name, info_framerate, bitrate_video,duration_video):
    info_video_on_ffprobe = check_video_from_ffprobe(info_video_ffprobe, info_code_name)
    info_video_on_mediainfo = check_video_from_mediainfo(info_video_mediainfo, info_framerate, bitrate_video,duration_video)
    info_video = merge_list(info_video_on_mediainfo,info_video_on_ffprobe,'Stream')
    return info_video


def merge_list(A,B,key):
    if len(A) >= len(B):
        temp_list = A
    else:
        temp_list = B
    flag = False
    for item_b in B:
        for id_a in range(0,len(temp_list)):
            if temp_list[id_a][key] == item_b[key]:
                temp_list[id_a] = dict(temp_list[id_a].items()+item_b.items())
                flag = True
        if flag == False:
            temp_list.append(item_b)
        else:
            flag = False
    return temp_list


def check_input(info_ffprobe, info_mediainfo):
    if info_ffprobe is None:
        return None
    if type(info_ffprobe) is dict:
        info_ffprobe = json.dumps(info_ffprobe)
    info_ffprobe = json.loads(info_ffprobe)
    if info_mediainfo is None:
        return None

    info_framerate = code_framerate()
    info_code_name = code_name()
    Audio_Array = []
    Video_Array = []
    for streams in info_ffprobe['streams']:
        if streams['codec_type'] == 'audio':
            Audio_Array.append(streams)
        elif streams['codec_type'] == 'video':
            Video_Array.append(streams)

    ###ERROR 1: loi khi 2 stream Video tro len - update chuc nang nay sau
    # duration_video = str(int(math.ceil(float(info_ffprobe['format']['duration']))))
    # interlaced = checkout_source_interlaced(info_mediainfo)
    # duration_video = str(info_ffprobe['format']['duration'])
    # bitrate_video = str(int(info_ffprobe['format']['bit_rate']) / 1024) + 'K'
    # info_ = {
    #     'Interlaced': interlaced,
    #     'Duration': duration_video,
    #     'Bitrate': bitrate_video
    # }
    ###END ERROR 1
    bitrate_video = str(int(info_ffprobe['format']['bit_rate']) / 1024)
    duration_video = str(info_ffprobe['format']['duration'])
    info_video = process_info_video(info_mediainfo,
                                    Video_Array,
                                    info_code_name,
                                    info_framerate,
                                    bitrate_video,
                                    duration_video)
    # return failed if can't get video info
    if not info_video:
        return None
    info_audio = check_audio(Audio_Array)
    Result_Audio = {'Audio': info_audio}
    Result_Video = {'Video': info_video}

    Result = dict(Result_Video.items() + Result_Audio.items())
    print Result
    return Result


def split_temp(input, output, array_code_name, array_framerate):
    cmnd = ['ffmpeg', '-i', '<INPUT>', '-c', 'copy', '-t', '60', '-map_metadata', '-1', '-y', '<OUTPUT>']
    sfileName = os.path.splitext(os.path.basename(input))[0]
    output_video = output + '/' + sfileName + '.mkv'
    print output_video
    for i in range(0, len(cmnd)):
        if cmnd[i] == '<INPUT>':
            cmnd[i] = input
        elif cmnd[i] == '<OUTPUT>':
            cmnd[i] = output_video
    print cmnd
    p = subprocess.Popen(cmnd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    info, error = p.communicate()
    if error:
        return error
    if not info:
        return None
    return output_video


def call_tool(cmnd):
    p = subprocess.Popen(cmnd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    info, error = p.communicate()
    if error:
        return error
    if not info:
        return None
    return info


if __name__ == '__main__':
    default_framerate = code_framerate()
    default_code_name = code_name()
    command_ = cmnd_Mediainfo()
    info__ = call_mediainfo(command_)
    # info___ = {"streams": [{"profile": "High", "codec_tag": "0x0000", "codec_type": "video", "codec_name": "h264", "codec_time_base": "1001/60000", "index": 0, "start_pts": 0, "disposition": {"comment": 0, "forced": 0, "lyrics": 0, "default": 1, "dub": 0, "original": 0, "karaoke": 0, "clean_effects": 0, "attached_pic": 0, "visual_impaired": 0, "hearing_impaired": 0}, "width": 1280, "pix_fmt": "yuv420p", "r_frame_rate": "30000/1001", "start_time": "0.000000", "time_base": "1/1000", "codec_tag_string": "[0][0][0][0]", "codec_long_name": "H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10", "display_aspect_ratio": "16:9", "sample_aspect_ratio": "1:1", "height": 720, "avg_frame_rate": "30000/1001", "level": 31, "bits_per_raw_sample": "8", "has_b_frames": 0}, {"index": 1, "sample_fmt": "fltp", "codec_tag": "0x0000", "start_pts": 0, "channel_layout": "stereo", "r_frame_rate": "0/0", "start_time": "0.000000", "time_base": "1/1000", "codec_tag_string": "[0][0][0][0]", "codec_type": "audio", "disposition": {"comment": 0, "forced": 0, "lyrics": 0, "default": 1, "dub": 0, "original": 0, "karaoke": 0, "clean_effects": 0, "attached_pic": 0, "visual_impaired": 0, "hearing_impaired": 0}, "codec_name": "aac", "codec_long_name": "AAC (Advanced Audio Coding)", "bits_per_sample": 0, "sample_rate": "44100", "codec_time_base": "1/44100", "channels": 2, "avg_frame_rate": "0/0"}], "format": {"tags": {"ENCODER": "Lavf56.4.101"}, "nb_streams": 2, "start_time": "0.000000", "format_long_name": "Matroska / WebM", "format_name": "matroska,webm", "filename": "/tmp/process/201411/ffc75b3aaa/test.mkv", "bit_rate": "3138268", "nb_programs": 0, "duration": "60.026000", "probe_score": 100, "size": "23547212"}}
    command__ = cmnd_ffprobe()
    info___ = call_ffprobe(command__)
    check_input(info___, info__)
    #split_temp('/store/SourceMKV/1080p_AuNor_TID_FHD.mkv','/store/Tool_Transcode_HDViet/0Trancode_new')