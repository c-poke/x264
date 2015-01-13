import os
import subprocess


def create_folder(directory):
    try:
        if not os.path.isfile(directory):
            if not os.path.exists(directory):
                os.makedirs(directory)
        else:
            os.remove(directory)
            os.makedirs(directory)
        return True, None
    except OSError as e:
        return False, e.message


def call_ffmpeg(input, number_segment, folder_output):
    sfileName = os.path.splitext(os.path.basename(input))[0]
    folder_content = folder_output + '/piece'
    output = folder_content + '/' + sfileName + '_%4d.mkv'
    Result_Create_Folder, error_create = create_folder(folder_content)
    if Result_Create_Folder == False:
        return {'e': 1, 'Error': 801, 'Description': error_create}

    cmnd = ['ffmpeg', '-i', input, '-vcodec', 'copy',
            '-map_metadata', '-1', '-map', '0:v', '-segment_time', number_segment,
            '-f', 'segment', '-y', output]
    p = subprocess.Popen(cmnd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = p.communicate()
    exitCode = p.returncode
    # error occurs at ffprobe
    # print out
    # part = 'parts:' + argv[3] + 's-' + argv[4] + 's'
    # cmnd = [
    #     'mkvmerge', '-o', argv[5], '--split', part, '--no-audio', '--no-global-tags', '--no-buttons', '--no-track-tags',
    #     '--no-chapters', '--no-subtitles', argv[2]
    # ]
    # result = utils.run_command(cmnd, False)
    if exitCode == 0:
        result = []
        for entry in os.listdir(folder_content):
            file = folder_content + '/' + entry
            if os.path.isfile(file):
                result.append(file)

    else:
        result = {'e': 1, 'Error': 650, 'Description': 'Split File Error'}
    return result

    # return info


def probe_video_file(filename):
    try:
        cmnd = ['ffprobe', '-select_streams', 'v', '-show_streams', '-show_format',
                '-loglevel', 'quiet', '-print_format', 'json', filename]
        p = subprocess.Popen(cmnd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = p.communicate()
        # error occurs at ffprobe
        return out
    except OSError as e:
        return None