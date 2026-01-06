"""
Helper utilities for core1 module
"""

import core1
import time

class Core1Manager:
    """Manager class for convenient core1 usage"""
    
    def __init__(self, auto_start_monitor=True):
        # Only init if not already done
        try:
            core1.init()
        except:
            pass  # Already initialized
            
        if auto_start_monitor:
            try:
                core1.start_monitoring()
            except:
                pass  # Already monitoring
        self._monitoring = auto_start_monitor
        self._callback_timer = None
    
    def start_callback_processing(self, interval_ms=50):
        """Start automatic callback processing using a timer"""
        try:
            from machine import Timer
            self._callback_timer = Timer(-1)
            self._callback_timer.init(
                period=interval_ms,
                mode=Timer.PERIODIC,
                callback=lambda t: core1.process_callbacks()
            )
        except ImportError:
            print("Warning: Timer not available, callbacks must be processed manually")
    
    def stop_callback_processing(self):
        """Stop automatic callback processing"""
        if self._callback_timer:
            self._callback_timer.deinit()
            self._callback_timer = None
    
    def echo(self, data, blocking=True, callback=None, timeout=5000):
        """Convenience method for echo command"""
        if isinstance(data, str):
            data = data.encode()
        
        if blocking and callback is None:
            result = core1.call(cmd_id=core1.CMD_ECHO, data=data, timeout=timeout)
            return result
        elif callback:
            return core1.call_async(cmd_id=core1.CMD_ECHO, callback=callback, 
                                   data=data, timeout=timeout)
        else:
            return core1.call_event(cmd_id=core1.CMD_ECHO, data=data, timeout=timeout)
    
    def add(self, a, b, blocking=True):
        """Convenience method for add command"""
        import struct
        data = struct.pack('ii', a, b)
        
        if blocking:
            result = core1.call(cmd_id=core1.CMD_ADD, data=data, timeout=1000)
            return struct.unpack('i', result[:4])[0]
        else:
            event = core1.call_event(cmd_id=core1.CMD_ADD, data=data, timeout=1000)
            return event
    
    def get_status(self):
        """Get Core1 status"""
        import struct
        result = core1.call(cmd_id=core1.CMD_STATUS, timeout=1000)
        free_heap = struct.unpack('I', result[:4])[0]
        return {'free_heap': free_heap}
    
    def delay(self, ms, callback=None):
        """Execute delay on Core1"""
        import struct
        data = struct.pack('I', ms)
        
        if callback:
            return core1.call_async(cmd_id=core1.CMD_DELAY, callback=callback,
                                   data=data, timeout=ms+1000)
        else:
            event = core1.call_event(cmd_id=core1.CMD_DELAY, data=data, 
                                    timeout=ms+1000)
            return event

# Singleton instance
_manager = None

def get_manager(auto_start=True):
    """Get or create the global Core1Manager instance"""
    global _manager
    if _manager is None:
        _manager = Core1Manager(auto_start_monitor=auto_start)
    return _manager

