import octoprint_arc_welder.log as log
import ray
from threading import Thread, Event

@ray.remote
class ArcWelderProcess(object):
    def __init__(self, process_args):
        self._process_args = process_args
        # add the progress received callback
        self._process_args.update({"on_progress_received": self._progress_received})
        self._progress = None
        self._results = None
        self._is_cancelled = False
        self._progress_event = Event()
        # initialize the logger now
        logging_configurator = log.LoggingConfigurator("arc_welder", "arc_welder.", "octoprint_arc_welder.")
        root_logger = logging_configurator.get_root_logger()
        # so that we can
        self.logger = logging_configurator.get_logger(__name__)

    def process_file(self):

        def _process_file(args):
            import PyArcWelder as converter
            try:
                self._results = converter.ConvertFile(args)
            except Exception as e:
                self.logger.exception("An unexpected exception occurred while welding the file!")
            self._progress_received(False)

        thread = Thread(target=_process_file, args=[self._process_args,])
        thread.daemon = True
        thread.start()
        return True

    def _progress_received(self, progress):
        self._progress_event.set()
        self._progress = progress
        return not self._is_cancelled

    def get_progress(self):
        self._progress_event.wait(1)
        self._progress_event.clear()
        return self._progress

    def cancel(self):
        self._is_cancelled = True
        return True

    def get_results(self):
        if self._progress is not None and not self._progress:
            return self._results
        return None