import os
import sys

from CeleryWorkers.libraries import utils


def call_encodeaudio(param):
    runText = param
    info = utils.run_command(runText, False)
    return info


def set_params(input_source, output_video, params):
    for index, param in enumerate(params):
        if param == "<input>":
            params[index] = input_source
        elif param == "<output>":
            params[index] = output_video
    return params


def main(Input_source, Format, output_video_root, params):
    sfileName = os.path.splitext(os.path.basename(Input_source))[0]
    output_file = ''
    if Format == 'AAC':
        output_file = output_video_root + '/' + sfileName + '.m4a'
    elif Format == 'AC3':
        output_file = output_video_root + '/' + sfileName + '.ac3'
    else:
        return {'e': 1, 'Error': '8xx', 'Description': 'Error param encode audio'}

    runText = set_params(Input_source, output_file, params)
    result, error = call_encodeaudio(runText)
    if result == 0:
        permission_file, error = utils.permission_file(output_file)
        if permission_file is True:
            result = {'e': 0, 'Data': {'Output': output_file}}
        else:
            result = {'e': 1, 'Error': 802, 'Description': 'Permission File Error'}
    else:
        result = {'e': 1, 'Error': 800, 'Description': 'Encode File Error'}
    return result


if __name__ == '__main__':
    main(sys.argv)