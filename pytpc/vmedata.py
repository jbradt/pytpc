from __future__ import division, print_function
import struct
import numpy as np
import pandas as pd


class VMEFile(object):

    def __init__(self, filename):
        self.fp = open(filename, 'rb')
        self.adc_events_seen = 0
        self.scaler_events_seen = 0

    def _read(self):
        # Find the next event
        while True:
            magic = struct.unpack('<H', self.fp.read(2))[0]
            if magic == 0xe238:
                break

        self.fp.seek(-4, 1)  # rewind to previous word, which identifies event type and length
        evthdr, magic = struct.unpack('<HH', self.fp.read(4))
        assert magic == 0xe238, 'magic is now wrong. Out of alignment?'

        if evthdr == 0x2025:
            # This is a scalar event
            self.scaler_events_seen += 1
            return None
        elif evthdr == 0x17fb:
            # evtlen = evthdr & 0xfff
            evtnum, timestamp, coinreg = struct.unpack('<III', self.fp.read(12))

            self.fp.seek(16, 1)  # skip next 16 bytes
            raw1 = np.fromfile(self.fp, dtype='<I', count=512)

            self.fp.seek(16, 1)  # skip next 16 bytes
            raw2 = np.fromfile(self.fp, dtype='<I', count=512)

            adc_data = np.zeros((3, 512), dtype='u4')
            adc_data[0] = (raw1 & 0x0fff0000) >> 16
            adc_data[1] = (raw1 & 0x0fff)
            adc_data[2] = (raw2 & 0x0fff0000) >> 16

            self.adc_events_seen += 1
            true_evtnum = evtnum - self.scaler_events_seen  # evt num is incremented even for a scaler buffer

            evtdict = {'evt_num': true_evtnum,
                       'timestamp': timestamp,
                       'coin_reg': coinreg,
                       'adc_data': adc_data}
            return evtdict
        else:
            raise IOError('Invalid header:', hex(evthdr))
