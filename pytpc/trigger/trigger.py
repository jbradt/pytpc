from .multiplicity import MultiplicityTrigger
from pytpc.utilities import read_lookup_table, find_exclusion_region
import os
import logging

__all__ = ['TriggerSimulator']

logger = logging.getLogger(__name__)


class TriggerSimulator(object):
    def __init__(self, config, xcfg_path=None):
        self.padmap = read_lookup_table(config['padmap_path'])  # maps (cobo, asad, aget, ch) -> pad
        self.reverse_padmap = {v: k for k, v in self.padmap.items()}  # maps pad -> (cobo, asad, aget, ch)
        if xcfg_path is not None:
            self.excluded_pads, _ = find_exclusion_region(xcfg_path, self.padmap)
            logger.info('Excluding pads from regions in file %s', os.path.basename(xcfg_path))
        else:
            self.excluded_pads = []

        self.badpads = set(self.excluded_pads)
        logger.info('%d pads will be excluded', len(self.badpads))

        self.trigger = MultiplicityTrigger(config, self.reverse_padmap)

    def process_event(self, evt):
        non_excluded_pads = set(evt.keys()) - self.badpads
        evt_for_trigger = {k: evt[k] for k in non_excluded_pads}

        trig = self.trigger.find_trigger_signals(evt_for_trigger)
        mult = self.trigger.find_multiplicity_signals(trig)
        did_trig = self.trigger.did_trigger(mult)

        return did_trig, len(evt)
