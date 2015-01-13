import sys
import json

from CeleryWorkers.libraries import EncodeAudio, Split, Join, EncodeVideo
import GetInfo


module = 'Split, Join, GetInfo, EncodeAudio, EncodeVideo'


def main():
    if len(sys.argv) <= 2:
        print('usage: <param1> <param2>')
        return {'e': 1, 'Error': 400, 'Description': 'Bad Request'}
    else:
        if sys.argv[1] == 'GetInfo':
            ffprobe, mediainfo = GetInfo.main(sys.argv)

            re = {'e': 0, 'Data': {'FFprobe': ffprobe, 'mediainfo': mediainfo}}
            print json.dumps(re)
            return json.dumps(re)

        elif sys.argv[1] == 'Split':
            print('usage Split: main.py <Feature> <input> <number_segment>  <Output>')
            if len(sys.argv) != 5:
                return {'e': 1, 'Error': 401, 'Description': 'Bad Request Split'}

            input = sys.argv[2]
            number_segment = sys.argv[3]
            #sum_segment = sys.argv[4]
            folder_output = sys.argv[4]
            value = Split.call_ffmpeg(input, number_segment, folder_output)
            return value

        elif sys.argv[1] == 'Join':
            print('usage Join: main.py <Feature> <array_input> <output>')
            if len(sys.argv) != 6:
                return {'e': 1, 'Error': 402, 'Description': 'Bad Request Join'}

            input = sys.argv[2]
            audio = sys.argv[3]
            folder_temp = sys.argv[4]
            output = sys.argv[5]
            value = Join.main(input, audio, folder_temp, output)
            return value

        elif sys.argv[1] == 'EncodeVideo':
            print('usage Encode Video: main.py '
                  '<Feature> <input> <profile> <framerate> <ScanType> <Scale> <Height> <output>')
            if len(sys.argv) != 9:
                return {'e': 1, 'Error': 403, 'Description': 'Bad Request Encode Video'}

            Input_video = sys.argv[2]
            Profile_video = sys.argv[3]
            Framerate = sys.argv[4]
            ScanType = sys.argv[5]
            Scale = sys.argv[6]
            Height = sys.argv[7]
            Output_video_root = sys.argv[8]
            value = EncodeVideo.main(Input_video, Profile_video, Framerate, ScanType, Scale, Height, Output_video_root)
            return value

        elif sys.argv[1] == 'EncodeAudio':
            print('usage Encode Audio: main.py <Feature> <input> <Format> <Bitrate> <Volume> <output>')
            if len(sys.argv) != 7:
                return {'e': 1, 'Error': 405, 'Description': 'Bad Request Encode Audio'}
            Input_source = sys.argv[2]
            Format = sys.argv[3]
            Bitrate = sys.argv[4]
            Volume = sys.argv[5]
            output_video_root = sys.argv[6]

            value = EncodeAudio.main(Input_source, Format, Bitrate, Volume, output_video_root)
            return value

        else:
            return {'e': 1, 'Error': 400, 'Description': 'Bad Request'}


if __name__ == '__main__':
    main()
