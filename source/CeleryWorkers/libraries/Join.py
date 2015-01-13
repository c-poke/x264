import os
import sys

from CeleryWorkers.libraries import utils


def call_mp4box(param):
    runText = param
    info = utils.run_command(runText, False)
    return info


def create_input(array_input):
    inputs = []
    videoExtensions = (".mp4",)
    for i in range(0, len(array_input)):
        if not os.path.isfile(array_input[i]):
            print("ERROR: File was not found:", array_input[i])
            return 0, array_input[i]
        fileName, fileExtension = os.path.splitext(os.path.basename(array_input[i]))
        if (i == 0):
            fileName = fileName + "_join"
            inputs.append('-add')
            inputs.append(array_input[i])
        else:
            if fileExtension in videoExtensions:
                inputs.append('-cat')
                inputs.append(array_input[i])
            else:
                inputs.append('-add')
                inputs.append(array_input[i])
    return inputs, None


def mux_video_audio(video, audio, output, params):
    runText = params
    runText.append('-add')
    runText.append(video)
    runText.append('-add')
    runText.append(audio)
    runText.append('-new')
    runText.append(output)
    result, err = call_mp4box(runText)
    return result, err


def main(input, audio, folder_temp, output, params):
    runText = list(params)
    array_input = input.split(',')
    #danger in here
    temp_video = array_input[0]
    input_audio = audio
    fileName, fileExtension = os.path.splitext(os.path.basename(temp_video))
    fileName = fileName + "_temp.mp4"
    output_temp = folder_temp + '/' + fileName
    result_input, file_error = create_input(array_input)
    if file_error is None:
        for i in range(0, len(result_input)):
            runText.append(result_input[i])
        runText.append('-new')
        runText.append(output_temp)
        result, err = call_mp4box(runText)
        if result == 0:
            result, err = mux_video_audio(output_temp, audio, output, params)
            if err:
                result = {'e': 1, 'Error': 700, 'Description': 'Join File Error'}
                return result
            result = {'e': 0, 'Data': {'Output': output}}
        else:
            result = {'e': 1, 'Error': 700, 'Description': 'Join File Error'}
    else:
        result = {'e': 1, 'Error': 404, 'File_Error': file_error, 'Description': 'File not found'}
    return result


if __name__ == '__main__':
    main(sys.argv)