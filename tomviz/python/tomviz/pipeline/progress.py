###############################################################################
# This source file is part of the Tomviz project, https://tomviz.org/.
# It is released under the 3-Clause BSD License, see "LICENSE".
###############################################################################
"""Progress reporters for the pipeline CLI. Three modes are supported, all
preserving the JSON wire format used by the legacy CLI so the future C++
external pipeline executor can consume them unchanged:

  - tqdm: console progress bar (interactive use)
  - socket: connect to a Unix domain socket and write JSON lines
  - files: write each JSON message to a numbered file in a directory
"""

import abc
import json
import os
import socket
import stat

from tqdm import tqdm


class ProgressBase(object):
    def started(self, op=None):
        self._operator_index = op

    def finished(self, op=None):
        pass


class TqdmProgress(ProgressBase):
    def __init__(self):
        self._maximum = None
        self._value = None
        self._message = None
        self._progress_bar = None

    @property
    def maximum(self):
        return self._maximum

    @maximum.setter
    def maximum(self, value):
        self._progress_bar = tqdm(total=value)
        self._maximum = value

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, value):
        if self._progress_bar is not None:
            self._progress_bar.update(value - self._progress_bar.n)
        self._value = value

    @property
    def message(self):
        return self._message

    @message.setter
    def message(self, msg):
        if self._progress_bar is not None:
            self._progress_bar.set_description(msg)
        self._message = msg

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        if self._progress_bar is not None:
            self._progress_bar.close()
        return False

    def finished(self, op=None):
        if self._progress_bar is not None:
            self._progress_bar.close()
            self._progress_bar = None


class JsonProgress(ProgressBase, metaclass=abc.ABCMeta):
    """Base class for the socket and files progress modes. Subclasses
    implement write() (emit one JSON message) and write_to_file()
    (persist a numpy data payload and return its path)."""

    @abc.abstractmethod
    def write(self, data):
        ...

    @abc.abstractmethod
    def write_to_file(self, data):
        ...

    def set_operator_index(self, index):
        self._operator_index = index

    @property
    def maximum(self):
        return self._maximum

    @maximum.setter
    def maximum(self, value):
        self.write({
            'type': 'progress.maximum',
            'operator': self._operator_index,
            'value': value,
        })
        self._maximum = value

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, value):
        self.write({
            'type': 'progress.step',
            'operator': self._operator_index,
            'value': value,
        })
        self._value = value

    @property
    def message(self):
        return self._message

    @message.setter
    def message(self, msg):
        self.write({
            'type': 'progress.message',
            'operator': self._operator_index,
            'value': msg,
        })
        self._message = msg

    @property
    def data(self):
        return self._data

    @data.setter
    def data(self, value):
        path = self.write_to_file(value)
        self.write({
            'type': 'progress.data',
            'operator': self._operator_index,
            'value': path,
        })
        self._data = value

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def started(self, op=None):
        super().started(op)
        msg = {'type': 'started'}
        if op is not None:
            msg['operator'] = op
        self.write(msg)

    def finished(self, op=None):
        super().started(op)
        msg = {'type': 'finished'}
        if op is not None:
            msg['operator'] = op
        self.write(msg)


class _WriteToFileMixin(object):
    def write_to_file(self, dataobject):
        # Importing lazily so a CLI run that never produces intermediate
        # data doesn't pull in HDF5 just to register the writer.
        from tomviz.io_emd import _write_emd
        filename = '%d.emd' % self._sequence_number
        path = os.path.join(os.path.dirname(self._path), filename)
        _write_emd(path, dataobject)
        self._sequence_number += 1
        return filename


class LocalSocketProgress(_WriteToFileMixin, JsonProgress):
    """Connect to a Unix domain socket and write JSON-newline messages
    consumed by the C++ Qt-side QLocalServer."""

    def __init__(self, socket_path):
        self._maximum = None
        self._value = None
        self._message = None
        self._connection = None
        self._path = socket_path
        self._sequence_number = 0

        try:
            mode = os.stat(self._path).st_mode
            if stat.S_ISSOCK(mode):
                self._uds_connect()
            else:
                raise Exception('Invalid progress path type.')
        except OSError as e:
            raise Exception(
                f"Progress path doesn't exist: {self._path}") from e

    def _uds_connect(self):
        self._connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._connection.connect(self._path)

    def write(self, data):
        encoded = ('%s\n' % json.dumps(data)).encode('utf8')
        self._connection.sendall(encoded)

    def __exit__(self, *exc):
        if self._connection is not None:
            self._connection.close()
        return False


class FilesProgress(_WriteToFileMixin, JsonProgress):
    """Write each JSON message as a separate numbered file in a directory
    that is being watched by the consumer (legacy on Mac/Windows)."""

    def __init__(self, path):
        self._path = path
        self._sequence_number = 0

    def write(self, data):
        file_path = os.path.join(self._path,
                                 'progress%d' % self._sequence_number)
        self._sequence_number += 1
        with open(file_path, 'w') as f:
            json.dump(data, f)


def make_progress(method: str, path: str | None = None):
    if method == 'tqdm':
        return TqdmProgress()
    if method == 'socket':
        if not path:
            raise Exception("'socket' progress mode requires a socket path")
        return LocalSocketProgress(path)
    if method == 'files':
        if not path:
            raise Exception("'files' progress mode requires a directory path")
        return FilesProgress(path)
    raise Exception(f'Unrecognized progress method: {method}')
