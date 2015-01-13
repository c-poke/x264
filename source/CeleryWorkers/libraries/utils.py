import os
import sys
import subprocess
from os import path


# def get_path_exists(string=None, file_type=None):
#     if string is None:
#         string = input()
#     while True:
#         if string == '':
#             print('Error')
#         else:
#             tmp = path.readpath(path.normpath(string.strip(' \'\"')))
#             if file_type == 'dir':
#                 if path.isdir(tmp):
#                     return tmp
#             elif file_type == 'file':
#                  if path.isfile(tmp):
#                     return tmp
#             else:
#                 if path.exists(tmp):
#                     return tmp
#
#
# string = input()


def run_command(command, show_output=False, display=False):
    # print ("Executing  :  %s " % command)
    # process = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if display:
        while True:
            nextline = process.stdout.readline()
            if nextline == '' and process.poll() is not None:
                break
            sys.stdout.write(nextline)
            sys.stdout.flush()

        output, stderr = process.communicate()
        exitCode = process.returncode
    else:
        output, stderr = process.communicate()
        exitCode = process.returncode

    if exitCode == 0:
        if show_output is True:
            return output, None
        else:
            return exitCode, None
    else:
        print "Error", stderr
        print "Failed to execute command %s" % command
        print exitCode, output
        raise Exception(output)


def create_folder(directory):
    try:
        if not os.path.exists(directory):
            os.makedirs(directory)
        return True, None
    except OSError as e:
        return False, e.message


def permission_file(file_path):
    try:
        if path.exists(file_path):
            os.chmod(file_path, 0777)
        return True, None
    except OSError as e:
        print (str(__name__) + " **** " + e.message + ": " + file_path)
        return False, e.message