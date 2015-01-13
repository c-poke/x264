import os
import subprocess

from CeleryWorkers import utilities


def split_file(input_file_path, output_folder, params):
    """
    Example:
    split_file(
        "/input/file.mp4",
        "100",
        "/output",
        ['ffmpeg', '-i', '<input>', '-vcodec', 'copy', '-map_metadata', '-1', '-map', '0:v', '-segment_time',
        '<segment_time>', '-f', 'segment', '-y', "<output>"])
    """
    # generate output pattern
    output_pattern = output_folder + '/' + utilities.generate_string(8) + '_%4d.mkv'
    # prepare command-line
    for index, param in enumerate(params):
        if param == "<input>":
            params[index] = input_file_path
        elif param == "<output>":
            params[index] = output_pattern
    cmnd = params
    # create thread
    p = subprocess.Popen(cmnd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = p.communicate()
    if p.returncode != 0:
        raise Exception(err)
    # get list file
    result = []
    for filename in os.listdir(output_folder):
        file_path = output_folder + '/' + filename
        if os.path.isfile(file_path) is True:
            result.append(file_path)

    return sorted(result)