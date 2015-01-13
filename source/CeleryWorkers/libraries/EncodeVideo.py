import os
import sys

from CeleryWorkers.libraries import utils


def call_encodevideo(param):
    runText = param
    info = utils.run_command(runText, False)
    return info


def main(Input_video, Output_video_root, params):
    sfileName = os.path.splitext(os.path.basename(Input_video))[0]
    output_video_folder = Output_video_root
    output_video = output_video_folder + '/' + sfileName + '.mp4'
    for index, param in enumerate(params):
        if param == "<input>":
            params[index] = Input_video
        elif param == '<output>':
            params[index] = output_video
    runText = params

    result, error = call_encodevideo(runText)
    if (result == 0):
        permission_file, error = utils.permission_file(output_video)
        if permission_file is True:
            result = {'e': 0, 'Data': {'Output': output_video} }
        else:
            result = {'e': 1, 'Error': 802, 'Description': 'Permission File Error'}
    else:
        result = {'e': 1, 'Error': 800, 'Description': 'Encode File Error'}

    return result


if __name__ == '__main__':
    main(sys.argv)