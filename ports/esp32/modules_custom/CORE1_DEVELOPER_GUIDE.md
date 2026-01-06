# Core1 Dual-Core API - Developer Guide

## Table of Contents
1. [Overview](#overview)
2. [Architecture](#architecture)
3. [C Implementation Details](#c-implementation-details)
4. [MicroPython API Reference](#micropython-api-reference)
5. [Usage Examples](#usage-examples)
6. [Peter Hinch Queue Integration](#peter-hinch-queue-integration)
7. [Shutdown and Recovery](#shutdown-and-recovery)
8. [Timing Considerations](#timing-considerations)
9. [Performance Characteristics](#performance-characteristics)
10. [Troubleshooting](#troubleshooting)

---

## Overview

The Core1 API provides a safe, efficient mechanism for MicroPython code running on ESP32-S3's Core 0 to execute operations on Core 1. This enables true parallel processing while maintaining Python's ease of use.

### Key Features

- **Three Response Modes**: Blocking, Callback, and Event-based
- **Peter Hinch Queue Integration**: True async/await support with uasyncio
- **Graceful Shutdown**: Clean resource cleanup and reinitialization
- **Thread-Safe**: All inter-core communication uses FreeRTOS primitives
- **Zero Python Objects in Core 1**: Prevents GIL issues and crashes
- **Extensible Command System**: Easy to add custom operations
- **State Management**: Track system lifecycle and health

### Design Philosophy

The API is designed around a fundamental constraint: **Python objects cannot be safely accessed from multiple threads**. Therefore, all data exchange between cores happens through value copies in FreeRTOS queues, and Python object creation is deferred until the MicroPython thread context.

---

## Architecture

### High-Level Design

```
┌─────────────────────────────────────┐
│         Core 0 (MicroPython)        │
│  ┌──────────────────────────────┐  │
│  │  MicroPython Application     │  │
│  └──────────┬───────────────────┘  │
│             │ Call API              │
│  ┌──────────▼───────────────────┐  │
│  │  modcore1.c (Python Binding) │  │
│  └──────────┬───────────────────┘  │
│             │                       │
│  ┌──────────▼───────────────────┐  │
│  │  Command Queue (FreeRTOS)    │  │
│  └──────────┬───────────────────┘  │
└─────────────┼───────────────────────┘
              │
              │ Queue Transfer
              │
┌─────────────▼───────────────────────┐
│         Core 1 (Processing)         │
│  ┌──────────────────────────────┐  │
│  │  core1_task_main()           │  │
│  │  - Receives commands         │  │
│  │  - Executes operations       │  │
│  │  - Sends responses           │  │
│  └──────────┬───────────────────┘  │
│             │                       │
│  ┌──────────▼───────────────────┐  │
│  │  Response Queue (FreeRTOS)   │  │
│  └──────────┬───────────────────┘  │
└─────────────┼───────────────────────┘
              │
┌─────────────▼───────────────────────┐
│  Core 0 (Response Processing)       │
│                                     │
│  ┌─────────────┬──────────────────┐│
│  │  Blocking   │  Monitor Task    ││
│  │  Wait Loop  │  (Async/Events)  ││
│  └─────────────┴──────────────────┘│
└─────────────────────────────────────┘
```

### Component Overview

#### Core Components

1. **Command Queue** (`cmd_queue`): Core 0 → Core 1
   - Fixed-size FreeRTOS queue
   - Stores `core1_command_t` structures
   - Thread-safe enqueue/dequeue

2. **Response Queue** (`resp_queue`): Core 1 → Core 0
   - Fixed-size FreeRTOS queue
   - Stores `core1_response_t` structures
   - Accessed by both blocking calls and monitor task

3. **Core 1 Task**: Runs on Core 1
   - Processes commands from queue
   - Executes requested operations
   - Sends responses back
   - Exits cleanly on shutdown request

4. **Monitor Task**: Runs on Core 0 (optional)
   - Polls response queue
   - Routes responses to callbacks/events
   - Schedules Peter Hinch queue notifications
   - Handles timeouts

5. **Pending Command Registry**: Tracks in-flight operations
   - Maps sequence numbers to response modes
   - Stores callback/event references
   - Stores Peter Hinch queue references
   - Implements timeout tracking

6. **Queue Put Buffer**: Deferred queue notifications
   - Stores pending queue.put_nowait() operations
   - Processed by `process_callbacks()`
   - Ensures queue operations happen in Python context
   - Max 16 pending items (configurable)

---

## C Implementation Details

### Data Structures

#### Command Structure
```c
typedef struct {
    uint16_t cmd_id;              // Command identifier
    uint32_t sequence;            // Unique sequence number
    core1_response_mode_t mode;   // BLOCKING, CALLBACK, or EVENT
    uint32_t timeout_ms;          // Timeout in milliseconds
    void* callback_ref;           // Python callback object (for CALLBACK mode)
    void* event_ref;              // Event object (for EVENT mode)
    uint8_t payload[128];         // Command data
} core1_command_t;
```

#### Response Structure
```c
typedef struct {
    uint32_t sequence;            // Matches command sequence
    core1_status_t status;        // Success/error code
    uint8_t payload[128];         // Response data
} core1_response_t;
```

#### Pending Command Entry
```c
typedef struct {
    uint32_t sequence;
    core1_response_mode_t mode;
    void* callback_ref;
    void* event_ref;
    uint64_t deadline_us;         // Absolute timeout timestamp
    bool active;
} pending_command_t;
```

### Core 1 Task Implementation

The Core 1 task runs an infinite loop processing commands:

```c
void core1_task_main(void *pvParameters) {
    core1_command_t cmd;
    core1_response_t resp;
    
    while (1) {
        // Wait for command (blocking)
        if (xQueueReceive(g_core1_state.cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            // Prepare response
            resp.sequence = cmd.sequence;
            resp.status = CORE1_OK;
            
            // Dispatch command
            switch (cmd.cmd_id) {
                case CMD_ECHO:
                    memcpy(resp.payload, cmd.payload, CORE1_MAX_PAYLOAD_SIZE);
                    break;
                    
                case CMD_ADD:
                    int32_t a = *(int32_t*)&cmd.payload[0];
                    int32_t b = *(int32_t*)&cmd.payload[4];
                    int32_t result = a + b;
                    memcpy(resp.payload, &result, sizeof(result));
                    break;
                    
                // ... additional commands
            }
            
            // Send response
            xQueueSend(g_core1_state.resp_queue, &resp, pdMS_TO_TICKS(100));
        }
    }
}
```

**Key Design Points:**
- Runs entirely on Core 1
- Never touches Python objects
- All data passed by value
- Blocking wait for commands (no busy polling)

### Monitor Task Implementation

The monitor task handles asynchronous response routing:

```c
void core1_monitor_task(void *pvParameters) {
    core1_response_t resp;
    core1_state_t* state = core1_get_state();
    
    while (1) {
        // Poll for responses
        if (xQueueReceive(state->resp_queue, &resp, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Find pending command
            pending_command_t* pending = core1_find_pending(resp.sequence);
            
            if (pending) {
                if (pending->mode == CORE1_MODE_CALLBACK) {
                    // Schedule callback execution in Python thread
                    core1_schedule_callback(pending->callback_ref, &resp);
                } else if (pending->mode == CORE1_MODE_EVENT) {
                    // Signal event object
                    core1_signal_event(pending->event_ref, &resp);
                } else if (pending->mode == CORE1_MODE_BLOCKING) {
                    // Put back for blocking wait to receive
                    xQueueSendToFront(state->resp_queue, &resp, 0);
                }
            }
        }
        
        // Check for timeouts
        uint64_t now = esp_timer_get_time();
        for (int i = 0; i < CORE1_MAX_PENDING; i++) {
            if (state->pending[i].active && now >= state->pending[i].deadline_us) {
                // Handle timeout based on mode
                if (state->pending[i].mode == CORE1_MODE_CALLBACK) {
                    core1_schedule_callback_timeout(state->pending[i].callback_ref);
                } else if (state->pending[i].mode == CORE1_MODE_EVENT) {
                    core1_signal_event_timeout(state->pending[i].event_ref);
                }
                core1_clear_pending(state->pending[i].sequence);
            }
        }
    }
}
```

**Key Design Points:**
- Runs on Core 0 (same core as MicroPython)
- Routes responses based on mode
- Schedules callbacks and queue notifications (doesn't execute them)
- Implements timeout handling
- Critical optimization: puts BLOCKING mode responses back in queue

### Thread Safety Guarantees

#### Python Object Isolation

**Problem**: Python objects can only be safely accessed from the Python thread.

**Solution**: Two-phase approach:

1. **In Monitor Task** (FreeRTOS context):
   ```c
   // Store raw response data, NOT Python objects
   callback_queue[tail].response = *resp;  // Copy struct
   callback_queue[tail].is_timeout = false;
   
   // Schedule Peter Hinch queue notification (if provided)
   if (pending->queue_ref != mp_const_none) {
       queue_put_item.event_obj = pending->event_ref;
       queue_put_item.queue_obj = pending->queue_ref;
       // Actual put happens later in process_callbacks()
   }
   ```

2. **In process_callbacks()** (Python context):
   ```c
   // NOW create Python objects
   mp_obj_t result = mp_obj_new_bytes(item->response.payload, size);
   mp_obj_t error = mp_obj_new_int(item->response.status);
   
   // Call callback with Python objects
   mp_call_function_n_kw(callback, 2, 0, args);
   
   // Execute Peter Hinch queue notifications
   queue.put_nowait(event_obj);
   ```

#### Queue Access Patterns

All queue operations are atomic and thread-safe via FreeRTOS:

- `xQueueSend()`: Non-blocking with timeout
- `xQueueReceive()`: Blocking or timed wait
- `xQueueSendToFront()`: Priority insertion for BLOCKING mode

#### Sequence Number Generation

```c
uint32_t core1_get_next_sequence(void) {
    return __atomic_fetch_add(&g_core1_state.sequence_counter, 1, __ATOMIC_SEQ_CST);
}
```

Uses GCC atomic built-ins for thread-safe increment.

---

## MicroPython API Reference

### Initialization Functions

#### `core1.init()`

Initialize the Core1 system. Must be called before any other Core1 operations.

**Parameters**: None

**Returns**: `None`

**Example**:
```python
import core1
core1.init()
```

**Behavior**:
- Creates command and response queues
- Spawns Core 1 task (pinned to Core 1)
- Initializes pending command registry
- Sets system state to INITIALIZED
- **Note**: Calling init() twice without shutdown() will log a warning and return without re-initializing

**Best Practice**: Call `shutdown()` before re-initializing:
```python
if core1.is_initialized():
    core1.shutdown()

core1.init()  # Clean initialization
```

#### `core1.start_monitoring()`

Start the monitor task for callback and event support.

**Parameters**: None

**Returns**: `None`

**Example**:
```python
core1.start_monitoring()
```

**When to use**:
- Required for callback mode (`call_async`)
- Required for event mode (`call_event`)
- Required for Peter Hinch queue integration
- Not needed for blocking mode (`call`)
- **Note**: Calling multiple times will log a warning but is safe

### Response Mode Functions

#### Mode A: Blocking (`core1.call`)

Synchronous call that blocks until response is received.

**Signature**:
```python
core1.call(cmd_id, data=None, timeout=5000) -> bytes
```

**Parameters**:
- `cmd_id` (int): Command identifier constant (e.g., `core1.CMD_ECHO`)
- `data` (bytes/str/int, optional): Command payload
- `timeout` (int, optional): Timeout in milliseconds (default: 5000)

**Returns**: `bytes` - Response payload (128 bytes)

**Raises**:
- `Core1TimeoutError`: Command timed out
- `Core1QueueFullError`: Command queue is full
- `Core1Error`: Core 1 returned error status

**Example**:
```python
import core1

core1.init()

# Echo command
result = core1.call(cmd_id=core1.CMD_ECHO, data=b"Hello World", timeout=1000)
print(result[:11])  # b'Hello World'

# Add two integers
import struct
data = struct.pack('ii', 100, 200)
result = core1.call(cmd_id=core1.CMD_ADD, data=data, timeout=1000)
sum_val = struct.unpack('i', result[:4])[0]
print(sum_val)  # 300

# Status query
result = core1.call(cmd_id=core1.CMD_STATUS, timeout=1000)
free_heap = struct.unpack('I', result[:4])[0]
print(f"Core 1 free heap: {free_heap} bytes")
```

**Use Cases**:
- Quick operations (<100ms)
- When you need the result immediately
- Sequential workflows
- When simplicity is more important than concurrency

**Performance**: 1-2ms round-trip latency

#### Mode B: Callback (`core1.call_async`)

Asynchronous call with callback notification.

**Signature**:
```python
core1.call_async(cmd_id, callback, data=None, timeout=5000) -> int
```

**Parameters**:
- `cmd_id` (int): Command identifier
- `callback` (callable): Function called with `(result: bytes, error: int/None)`
- `data` (bytes/str/int, optional): Command payload
- `timeout` (int, optional): Timeout in milliseconds

**Returns**: `int` - Sequence number of command

**Callback Signature**:
```python
def callback(result: bytes, error: int | None):
    if error is None:
        # Success - result contains response payload
        process(result)
    else:
        # Error or timeout - error contains error code
        handle_error(error)
```

**Example**:
```python
import core1

core1.init()
core1.start_monitoring()  # Required for callbacks

results = []

def handle_response(result, error):
    if error is None:
        results.append(result)
        print(f"Got result: {result[:10]}")
    else:
        print(f"Error: {error}")

# Send async command
seq = core1.call_async(
    cmd_id=core1.CMD_ECHO, 
    callback=handle_response,
    data=b"Async operation"
)

print(f"Command sent with sequence {seq}")

# Process callbacks in main loop
import time
while len(results) < 1:
    time.sleep(0.01)
    core1.process_callbacks()

print("Done!")
```

**Multiple Concurrent Operations**:
```python
def process_result(result, error):
    if error is None:
        print(f"Processed: {result[:20]}")

# Send multiple async operations
for i in range(10):
    core1.call_async(
        cmd_id=core1.CMD_ECHO,
        callback=process_result,
        data=f"Operation {i}".encode()
    )

# Process all callbacks
for _ in range(100):  # Poll for reasonable time
    count = core1.process_callbacks()
    if count > 0:
        print(f"Processed {count} callbacks")
    time.sleep(0.01)
```

**Use Cases**:
- Long-running operations
- Multiple concurrent operations
- When you want to continue processing while waiting
- Event-driven architectures

**Important**: You must call `core1.process_callbacks()` periodically to execute callbacks!

#### Mode C: Event (`core1.call_event`)

Event-based asynchronous call with manual result retrieval.

**Signature**:
```python
core1.call_event(cmd_id, data=None, timeout=5000, queue=None) -> Event
```

**Parameters**:
- `cmd_id` (int): Command identifier
- `data` (bytes/str/int, optional): Command payload
- `timeout` (int, optional): Timeout in milliseconds
- `queue` (optional): Peter Hinch Queue for asyncio integration

**Returns**: `Event` object

**Event Methods**:
- `is_ready() -> bool`: Check if result is available
- `get_result(timeout=0) -> bytes`: Get result (blocking with optional timeout)
- `sequence() -> int`: Get unique sequence number

**Example - Polling**:
```python
import core1
import time

core1.init()
core1.start_monitoring()  # Required for events

# Create event
event = core1.call_event(cmd_id=core1.CMD_ECHO, data=b"Event test")

# Poll until ready
while not event.is_ready():
    time.sleep(0.01)
    print("Waiting...")

# Get result
result = event.get_result()
print(f"Result: {result[:10]}")
```

**Example - With Peter Hinch Queue (Async)**:
```python
import core1
import uasyncio as asyncio
from primitives import Queue

async def main():
    core1.init()
    core1.start_monitoring()
    
    result_queue = Queue()
    
    # Create event with queue
    event = core1.call_event(
        cmd_id=core1.CMD_ECHO,
        data=b"Async event",
        queue=result_queue
    )
    
    # Await result (no polling!)
    event = await result_queue.get()
    result = event.get_result()
    print(f"Result: {result}")

asyncio.run(main())
```

Note: Queue integration requires `process_callbacks()` to be called regularly (see Peter Hinch Queue Integration section).
print(f"Result: {result[:10]}")
```

**Example - Blocking with Timeout**:
```python
event = core1.call_event(cmd_id=core1.CMD_DELAY, data=struct.pack('I', 100))

# Block until ready or timeout
try:
    result = event.get_result(timeout=2000)  # 2 second timeout
    print("Operation completed")
except core1.Core1TimeoutError:
    print("Timed out waiting for result")
```

**Example - Multiple Concurrent Events**:
```python
# Launch multiple operations
events = []
for i in range(5):
    event = core1.call_event(
        cmd_id=core1.CMD_ECHO,
        data=f"Event {i}".encode()
    )
    events.append(event)

# Wait for all to complete
while not all(e.is_ready() for e in events):
    time.sleep(0.01)

# Retrieve all results
results = [e.get_result() for e in events]
print(f"Got {len(results)} results")
```

**Use Cases**:
- When you need to check results at your own pace
- Multiple operations where you collect results later
- Integration with custom event loops
- When callback overhead is undesirable

---

### Support Functions

#### `core1.process_callbacks() -> int`

Process pending callbacks and queue notifications. Must be called regularly when using callback mode or Peter Hinch queue integration.

**Returns**: Number of callbacks processed

**Example**:
```python
import time

# In your main loop
while running:
    count = core1.process_callbacks()
    if count > 0:
        print(f"Processed {count} callbacks")
    time.sleep(0.01)
```

**When to call**:
- **Required for**: Callback mode (`call_async`), Peter Hinch queue notifications
- **Not needed for**: Blocking mode (`call`)
- **Event mode**: Only needed if using queues

**How often**: Every 10-50ms for responsive callbacks

**What it does**:
1. Executes pending callbacks from completed async operations
2. Puts Event objects into Peter Hinch queues
3. Releases Python object references

**Note**: The helper module's `start_callback_processing()` automates this.

---

### Helper Module

The `core1_helper` module provides a convenient high-level interface.

#### Core1Manager Class

```python
from core1_helper import Core1Manager

# Create manager (auto-initializes)
mgr = Core1Manager()

# Or get singleton
from core1_helper import get_manager
mgr = get_manager()
```

**Methods**:

```python
# Echo command
result = mgr.echo("Hello")  # Returns bytes

# Add two numbers
sum_val = mgr.add(42, 58)  # Returns int (100)

# Get status
status = mgr.get_status()  # Returns dict
print(status['free_heap'])

# Delay on Core 1
def on_delay_done(result, error):
    print("Delay finished!")

mgr.delay(500, callback=on_delay_done)  # 500ms delay

# Auto-process callbacks
mgr.start_callback_processing(interval_ms=50)
# Callbacks now execute automatically every 50ms
```

---

### Shutdown and State Management Functions

#### `core1.shutdown(timeout=5000, force=False)`

Shutdown the Core1 system and release all resources.

**Parameters**:
- `timeout` (int, optional): Maximum time in milliseconds to wait (default: 5000)
- `force` (bool, optional): If True, immediately terminate (default: False)

**Returns**: `None`

**Example**:
```python
# Graceful shutdown
core1.shutdown()

# With custom timeout
core1.shutdown(timeout=10000)

# Force immediate shutdown
core1.shutdown(force=True)
```

**Behavior**:
1. Sets system state to SHUTTING_DOWN
2. Stops monitor task
3. Stops Core 1 task
4. Drains and deletes queues
5. Clears pending commands
6. Resets state to UNINITIALIZED

#### `core1.stop_monitoring(timeout=5000)`

Stop only the monitoring task, leaving Core 1 task running.

**Parameters**:
- `timeout` (int, optional): Maximum time to wait (default: 5000)

**Returns**: `None`

**Example**:
```python
core1.stop_monitoring()

# Core 1 still works for blocking calls
result = core1.call(cmd_id=core1.CMD_ECHO, data=b"test")
```

**Use Case**: Switch from async/event mode back to blocking-only mode.

#### `core1.is_initialized() -> bool`

Check if the Core1 system is initialized.

**Returns**: `True` if initialized, `False` otherwise

**Example**:
```python
if not core1.is_initialized():
    core1.init()

result = core1.call(...)
```

#### `core1.get_state() -> int`

Get the current system state.

**Returns**: Integer representing system state

**State Constants**:
- `core1.STATE_UNINITIALIZED` (0)
- `core1.STATE_INITIALIZED` (1)
- `core1.STATE_RUNNING` (2)
- `core1.STATE_SHUTTING_DOWN` (3)
- `core1.STATE_ERROR` (4)

**Example**:
```python
state = core1.get_state()

if state == core1.STATE_UNINITIALIZED:
    core1.init()
elif state == core1.STATE_SHUTTING_DOWN:
    print("Shutdown in progress, please wait")
```

---

## Usage Examples

### Example 1: Simple Blocking Operations

```python
import core1
import struct

# Initialize
core1.init()

try:
    # Perform calculations on Core 1
    def add_on_core1(a, b):
        data = struct.pack('ii', a, b)
        result = core1.call(cmd_id=core1.CMD_ADD, data=data, timeout=1000)
        return struct.unpack('i', result[:4])[0]
    
    # Use it
    print(add_on_core1(123, 456))  # 579
    print(add_on_core1(-100, 200))  # 100
finally:
    # Always cleanup
    core1.shutdown()
```

### Example 2: Parallel Data Processing

```python
import core1
import time

core1.init()
core1.start_monitoring()

# Process multiple items in parallel
items = [f"Item {i}" for i in range(20)]
processed = []

def process_item(result, error):
    if error is None:
        processed.append(result)

# Submit all items
for item in items:
    core1.call_async(
        cmd_id=core1.CMD_ECHO,
        callback=process_item,
        data=item.encode()
    )

# Wait for all to complete
while len(processed) < len(items):
    core1.process_callbacks()
    time.sleep(0.01)

print(f"Processed {len(processed)} items")

# Cleanup
core1.shutdown()
```

### Example 3: Long-Running Operation with Progress

```python
import core1
import time
import struct

core1.init()
core1.start_monitoring()

# Start long operation
def on_complete(result, error):
    if error is None:
        print("Operation completed successfully!")
    else:
        print(f"Operation failed with error {error}")

seq = core1.call_async(
    cmd_id=core1.CMD_DELAY,
    callback=on_complete,
    data=struct.pack('I', 5000),  # 5 second delay
    timeout=10000
)

print(f"Operation {seq} started...")

# Do other work while waiting
for i in range(50):
    print(f"Main loop iteration {i}")
    core1.process_callbacks()
    time.sleep(0.1)

# Cleanup
core1.shutdown()
```

### Example 4: Event-Based Workflow

```python
import core1
import time

core1.init()
core1.start_monitoring()

# Launch operations
event1 = core1.call_event(cmd_id=core1.CMD_ECHO, data=b"First")
event2 = core1.call_event(cmd_id=core1.CMD_ECHO, data=b"Second")
event3 = core1.call_event(cmd_id=core1.CMD_ECHO, data=b"Third")

# Do other work...
print("Operations in progress...")

# Check status
print(f"Event 1 ready: {event1.is_ready()}")
print(f"Event 2 ready: {event2.is_ready()}")

# Wait for specific event
result1 = event1.get_result(timeout=1000)
print(f"First result: {result1[:5]}")

# Wait for all
while not (event2.is_ready() and event3.is_ready()):
    time.sleep(0.01)

print("All operations complete!")

# Cleanup
core1.shutdown()
```

### Example 5: Error Handling

```python
import core1
import struct

core1.init()

try:
    # This will timeout
    result = core1.call(
        cmd_id=core1.CMD_DELAY,
        data=struct.pack('I', 10000),  # 10 second delay
        timeout=1000  # 1 second timeout
    )
except core1.Core1TimeoutError:
    print("Operation timed out (expected)")
except core1.Core1QueueFullError:
    print("Command queue is full, try again later")
except core1.Core1Error as e:
    print(f"Core1 error: {e}")
finally:
    core1.shutdown()
```

### Example 6: Using the Helper Module

```python
from core1_helper import get_manager
import time

# Get manager instance
mgr = get_manager()

# Simple operations
print(mgr.echo("Hello from helper!"))
print(f"100 + 200 = {mgr.add(100, 200)}")

# Get Core 1 status
status = mgr.get_status()
print(f"Core 1 has {status['free_heap']} bytes free")

# Async operation with auto-processing
mgr.start_callback_processing(interval_ms=50)

def on_delay(result, error):
    print("Delay completed!")

mgr.delay(1000, callback=on_delay)
print("Delay started, will callback automatically")

time.sleep(2)  # Callbacks process automatically

# Note: Helper manager handles shutdown via __del__, 
# but explicit shutdown is recommended:
# core1.shutdown()
```

---

## Peter Hinch Queue Integration

The Core1 API integrates with Peter Hinch's `uasyncio` Queue class to provide true event-driven async/await support.

### Why Use Queues?

**Without Queue (Polling)**:
```python
event = core1.call_event(cmd_id=core1.CMD_ECHO, data=b"test")

# Must poll
while not event.is_ready():
    await asyncio.sleep_ms(10)  # Inefficient!

result = event.get_result()
```

**With Queue (Event-Driven)**:
```python
from primitives import Queue

result_queue = Queue()
event = core1.call_event(
    cmd_id=core1.CMD_ECHO,
    data=b"test",
    queue=result_queue
)

# True async - no polling!
event = await result_queue.get()
result = event.get_result()
```

### How It Works

1. **Event Creation**: Pass a Peter Hinch Queue to `call_event()`
2. **Monitor Task**: Detects response and schedules queue.put_nowait()
3. **process_callbacks()**: Executes queue.put_nowait() in Python context
4. **Async Code**: Awaits queue.get() without polling

**Key Design**: Queue operations happen in Python thread context to avoid GIL issues.

### Basic Usage

```python
import core1
import uasyncio as asyncio
from primitives import Queue

async def echo_async(data):
    result_queue = Queue()
    
    event = core1.call_event(
        cmd_id=core1.CMD_ECHO,
        data=data,
        queue=result_queue
    )
    
    # Await result
    event = await result_queue.get()
    return event.get_result()

async def main():
    core1.init()
    core1.start_monitoring()
    
    # Use like any async function
    result = await echo_async(b"Hello async!")
    print(result[:12])

asyncio.run(main())
```

### Processing Requirements

Queue notifications require `process_callbacks()` to be called regularly:

**Option 1: Background Task**
```python
async def callback_processor():
    while True:
        core1.process_callbacks()
        await asyncio.sleep_ms(10)

async def main():
    # Start processor in background
    asyncio.create_task(callback_processor())
    
    # Now use queue-based operations
    result = await echo_async(b"test")
```

**Option 2: Helper Module Auto-Processing**
```python
from core1_helper import get_manager

mgr = get_manager()
mgr.start_callback_processing(interval_ms=10)

# Now process_callbacks() happens automatically
```

### Multiple Concurrent Operations

```python
async def process_many(items):
    result_queue = Queue()
    
    # Submit all at once
    for item in items:
        core1.call_event(
            cmd_id=core1.CMD_ECHO,
            data=item.encode(),
            queue=result_queue
        )
    
    # Collect as they complete
    results = []
    for _ in range(len(items)):
        event = await result_queue.get()
        results.append(event.get_result())
    
    return results

# Process 10 items concurrently
items = [f"Item {i}" for i in range(10)]
results = await process_many(items)
```

### With asyncio.gather()

```python
async def echo_async(data):
    result_queue = Queue()
    event = core1.call_event(cmd_id=core1.CMD_ECHO, data=data, queue=result_queue)
    event = await result_queue.get()
    return event.get_result()

# Run multiple operations in parallel
results = await asyncio.gather(
    echo_async(b"First"),
    echo_async(b"Second"),
    echo_async(b"Third")
)
```

### Error Handling

If queue.put_nowait() fails (queue full), the system:
1. Retries up to 10 times
2. Prints warning after max retries
3. Event remains accessible via polling (graceful fallback)

```python
# Small queue can fill up
result_queue = Queue(maxsize=2)

# Submit 5 operations
for i in range(5):
    core1.call_event(cmd_id=core1.CMD_ECHO, data=f"{i}".encode(), queue=result_queue)

# Process to drain queue
for _ in range(10):
    core1.process_callbacks()
    try:
        event = result_queue.get_nowait()
        print(f"Got: {event.sequence()}")
    except:
        pass
    await asyncio.sleep_ms(50)
```

### Performance

- **Latency**: 10-100ms typical (vs 10ms+ polling)
- **CPU Usage**: Minimal (event-driven vs continuous polling)
- **Memory**: ~16 events max in queue put buffer

### Limitations

1. **Requires process_callbacks()**: Must be called regularly (every 10-50ms)
2. **Max 16 concurrent queued events**: Configurable in C code
3. **Order not guaranteed**: Multiple events may arrive out of order
4. **One queue per event**: Can't broadcast to multiple queues

For complete details, see [PETER_HINCH_QUEUE_INTEGRATION.md](PETER_HINCH_QUEUE_INTEGRATION.md)

---

## Shutdown and Recovery

The Core1 API provides comprehensive lifecycle management with graceful shutdown and reinitialization support.

### Basic Shutdown

```python
import core1

# Initialize and use
core1.init()
result = core1.call(cmd_id=core1.CMD_ECHO, data=b"test")

# Shutdown when done
core1.shutdown()
```

### Shutdown Modes

#### Graceful Shutdown (Default)

Waits for operations to complete before stopping:

```python
core1.shutdown(timeout=5000)  # Wait up to 5 seconds
```

**Process**:
1. Stop accepting new commands
2. Stop monitor task (waits for exit)
3. Stop Core 1 task (waits for current operation)
4. Drain and delete queues
5. Clear pending commands
6. Reset to UNINITIALIZED state

#### Force Shutdown

Immediately terminates all tasks:

```python
core1.shutdown(force=True)  # Immediate termination
```

**Use when**:
- System is hung
- Quick cleanup needed
- Error recovery

### Separate Monitor Control

Stop only the monitoring task:

```python
core1.init()
core1.start_monitoring()

# Use async operations...

# Stop monitoring, keep Core 1 running
core1.stop_monitoring()

# Blocking calls still work
result = core1.call(cmd_id=core1.CMD_ECHO, data=b"test")

# Full shutdown later
core1.shutdown()
```

### Reinitialization

After shutdown, you can reinitialize:

```python
# First use
core1.init()
result1 = core1.call(cmd_id=core1.CMD_ECHO, data=b"first")
core1.shutdown()

# Second use
core1.init()  # Fresh start
result2 = core1.call(cmd_id=core1.CMD_ECHO, data=b"second")
core1.shutdown()
```

### State Management

Check system state before operations:

```python
# Check if initialized
if not core1.is_initialized():
    core1.init()

# Get current state
state = core1.get_state()

if state == core1.STATE_UNINITIALIZED:
    print("Need to initialize")
elif state == core1.STATE_INITIALIZED:
    print("Ready to use")
elif state == core1.STATE_SHUTTING_DOWN:
    print("Shutdown in progress")
```

**State Constants**:
- `STATE_UNINITIALIZED` (0)
- `STATE_INITIALIZED` (1)
- `STATE_RUNNING` (2)
- `STATE_SHUTTING_DOWN` (3)
- `STATE_ERROR` (4)

### Error Recovery Pattern

```python
try:
    core1.init()
    result = core1.call(cmd_id=core1.CMD_ECHO, data=b"test")
except Exception as e:
    print(f"Error: {e}")
    # Force shutdown to recover
    core1.shutdown(force=True)
    
    # Reinitialize
    time.sleep(0.1)
    core1.init()
    print("System recovered")
```

### Resource Management

Always shutdown in finally blocks:

```python
def process_data(items):
    core1.init()
    try:
        results = []
        for item in items:
            result = core1.call(cmd_id=core1.CMD_ECHO, data=item)
            results.append(result)
        return results
    finally:
        core1.shutdown()  # Always cleanup
```

### Verbose Logging

Shutdown operations log detailed progress:

```
I (12345) MODCORE1: [SHUTDOWN] Starting shutdown (timeout=5000 ms, force=0)
I (12346) MODCORE1: [SHUTDOWN] Step 1: Stopping monitor task
I (12347) MODCORE1: [MONITOR] Stop requested, exiting task
I (12348) MODCORE1: [MONITOR] Task exiting cleanly
I (12400) MODCORE1: [SHUTDOWN] Monitor task stopped
I (12401) MODCORE1: [SHUTDOWN] Step 2: Stopping Core 1 task
I (12402) MODCORE1: [CORE1] Shutdown requested, exiting task
I (12403) MODCORE1: [CORE1] Task exiting cleanly
I (12500) MODCORE1: [SHUTDOWN] Core 1 task exited naturally
I (12501) MODCORE1: [SHUTDOWN] Step 3: Cleaning up queues
W (12502) MODCORE1: [SHUTDOWN] Drained 2 commands from command queue
I (12503) MODCORE1: [SHUTDOWN] Step 4: Clearing pending commands
W (12504) MODCORE1: [SHUTDOWN] Cleared 1 pending commands
I (12505) MODCORE1: [SHUTDOWN] Step 5: Resetting state
I (12506) MODCORE1: [SHUTDOWN] Shutdown complete
```

### Timing Considerations

**Graceful shutdown timing**:
- Monitor task: 50-200ms
- Core 1 task: 100-500ms (depends on current operation)
- Queue cleanup: <10ms
- Total: 200-700ms typical

**Force shutdown timing**:
- Task deletion: <10ms
- Queue cleanup: <10ms
- Total: <50ms

### What Happens to Pending Operations

During shutdown:
- **Pending callbacks**: Discarded (logged as warning)
- **Pending events**: Marked as inactive
- **Queued commands**: Drained from queues
- **Blocked Python threads**: Timeout naturally

For complete details, see [SHUTDOWN_RECOVERY_GUIDE.md](SHUTDOWN_RECOVERY_GUIDE.md)

---

## Timing Considerations

### Understanding the Timing Flow

When you send a command to Core 1, multiple steps occur:

1. **Command Enqueue** (~10-50μs): Python → Command Queue
2. **Core 1 Wakes** (~100-500μs): FreeRTOS scheduler
3. **Command Processing** (variable): Depends on command
4. **Response Enqueue** (~10-50μs): Core 1 → Response Queue
5. **Response Routing** (~100-500μs): Monitor task or blocking wait

**Total typical latency**: 1-3ms for simple commands

### Startup Timing

After calling `core1.init()` and `core1.start_monitoring()`, allow time for tasks to fully start:

```python
core1.init()
time.sleep(0.1)  # Allow Core 1 task to start

core1.start_monitoring()
time.sleep(0.1)  # Allow monitor task to start
```

**Why this matters**: If you immediately send commands after init, they may queue up before tasks are ready, causing apparent delays.

### Mode-Specific Timing Issues

#### Blocking Mode

- **Issue**: If monitor task is running, it might consume responses meant for blocking calls
- **Solution**: Monitor task detects BLOCKING mode and puts responses back in queue
- **Recommendation**: If using ONLY blocking mode, don't start monitoring

```python
core1.init()
# Don't call start_monitoring() if only using blocking mode
result = core1.call(...)  # Works fine
```

#### Callback Mode

- **Issue**: Callbacks aren't executed automatically
- **Solution**: You must call `process_callbacks()` regularly

```python
# WRONG - callbacks never execute
core1.call_async(cmd_id=..., callback=my_callback)
time.sleep(10)  # Callback never called!

# CORRECT - process callbacks
core1.call_async(cmd_id=..., callback=my_callback)
while not done:
    core1.process_callbacks()  # Execute pending callbacks
    time.sleep(0.01)
```

**Best Practice**: Use a timer for automatic processing:

```python
from core1_helper import get_manager
mgr = get_manager()
mgr.start_callback_processing(interval_ms=50)
# Now callbacks execute automatically every 50ms
```

#### Event Mode

- **Issue**: Event might not be ready immediately after creation
- **Solution**: Always check `is_ready()` or use `get_result(timeout=...)`

```python
# WRONG - assuming immediate readiness
event = core1.call_event(cmd_id=core1.CMD_ECHO, data=b"Test")
result = event.get_result()  # Might fail!

# CORRECT - polling
event = core1.call_event(cmd_id=core1.CMD_ECHO, data=b"Test")
while not event.is_ready():
    time.sleep(0.01)
result = event.get_result()

# BETTER - blocking with timeout
event = core1.call_event(cmd_id=core1.CMD_ECHO, data=b"Test")
result = event.get_result(timeout=1000)  # Wait up to 1 second
```

### Monitor Task Polling Interval

The monitor task polls every 10ms by default. For very latency-sensitive applications, this can be adjusted in `core1_api.c`:

```c
// In core1_monitor_task()
xQueueReceive(state->resp_queue, &resp, pdMS_TO_TICKS(10))
//                                                      ^^
// Change this value (milliseconds)
```

**Trade-off**: Lower = faster response but higher CPU usage

### Multiple Operations Timing

When sending multiple commands rapidly:

```python
# Send commands
for i in range(100):
    core1.call_async(cmd_id=..., callback=...)

# Process with adequate delays
for _ in range(1000):
    count = core1.process_callbacks()
    if count > 0:
        print(f"Processed {count} callbacks")
    time.sleep(0.01)  # Don't busy-wait
```

**Why sleep?**: Gives monitor task and Core 1 task time to process

### Cleanup Between Operations

When switching between modes or running repeated operations:

```python
# Clear any pending callbacks before starting new operations
core1.process_callbacks()
time.sleep(0.05)  # Let any final responses process

# Now start fresh
event = core1.call_event(...)
```

This prevents callbacks from previous operations interfering with new ones.

---

## Performance Characteristics

### Latency

| Operation | Typical Latency |
|-----------|----------------|
| Blocking call (echo) | 1-2ms |
| Callback scheduling | 50-100μs overhead |
| Event creation | 10-20μs |
| Response routing (monitor) | 100-500μs |

### Throughput

- **Command queue depth**: 16 commands
- **Sustained throughput**: ~1000 commands/second
- **Burst capacity**: Up to queue depth before blocking

### Memory Overhead

**Per Operation**:
- Blocking: ~120 bytes (temporary)
- Callback: ~200 bytes (until callback executed)
- Event: ~250 bytes (until result retrieved)
- Event with queue: ~280 bytes (until queue notification processed)

**Static Overhead**:
- Command queue: ~2KB
- Response queue: ~2KB
- Pending registry: ~1KB
- Queue put buffer: ~1KB (max 16 items)
- Total: ~6-8KB

### CPU Usage

- **Core 1 task**: Negligible when idle, 100% of Core 1 when processing
- **Monitor task**: ~1-5% of Core 0 (depends on polling interval)
- **MicroPython overhead**: ~0.5% for callback processing

---

## Troubleshooting

### Command Timeouts

**Symptom**: `Core1TimeoutError` raised frequently

**Possible Causes**:
1. Timeout too short for operation
2. Core 1 task not running
3. Queue full (commands backing up)
4. Core 1 task blocked/crashed

**Solutions**:
```python
# 1. Increase timeout
result = core1.call(cmd_id=..., timeout=10000)  # 10 seconds

# 2. Check Core 1 task is running
result = core1.call(cmd_id=core1.CMD_STATUS, timeout=1000)
# If this fails, Core 1 task isn't running

# 3. Check for queue full errors
try:
    core1.call(...)
except core1.Core1QueueFullError:
    time.sleep(0.1)  # Wait for queue to drain
    core1.call(...)  # Retry
```

### Callbacks Not Executing

**Symptom**: Callbacks never called

**Causes**:
1. Monitor task not started
2. Not calling `process_callbacks()`
3. Callback has wrong signature

**Solutions**:
```python
# 1. Ensure monitoring started
core1.start_monitoring()
time.sleep(0.1)

# 2. Process callbacks regularly
def my_callback(result, error):  # Must accept 2 args!
    print(f"Got result: {result}")

core1.call_async(cmd_id=..., callback=my_callback)

while True:
    core1.process_callbacks()  # Don't forget this!
    time.sleep(0.01)
```

### Events Never Ready

**Symptom**: `event.is_ready()` always returns `False`

**Causes**:
1. Monitor task not started
2. Not enough time for processing
3. Command failed/timed out

**Solutions**:
```python
# 1. Ensure monitor running
core1.start_monitoring()
time.sleep(0.1)

# 2. Wait adequately
event = core1.call_event(...)
time.sleep(0.1)  # Give time to process

for i in range(100):  # Reasonable retry limit
    if event.is_ready():
        break
    time.sleep(0.05)
else:
    print("Event never became ready - possible timeout")

# 3. Check result for errors
try:
    result = event.get_result(timeout=1000)
except core1.Core1Error as e:
    print(f"Operation failed: {e}")
```

### Memory Issues

**Symptom**: `MemoryError` or system instability

**Causes**:
1. Too many pending operations
2. Not retrieving event results
3. Callback objects not being released
4. Queue put buffer full (16 max)

**Solutions**:
```python
# 1. Limit concurrent operations
MAX_PENDING = 10
active_events = []

def send_command(data):
    if len(active_events) < MAX_PENDING:
        event = core1.call_event(cmd_id=..., data=data)
        active_events.append(event)
    else:
        print("Too many pending, waiting...")
        
# 2. Clean up completed events
active_events = [e for e in active_events if not e.is_ready()]

# 3. Process callbacks promptly
core1.process_callbacks()  # Releases callback references
```

### Peter Hinch Queue Issues

**Symptom**: Events not appearing in queue, or "queue full" warnings

**Causes**:
1. Not calling `process_callbacks()` regularly
2. Queue maxsize too small
3. Not consuming from queue fast enough

**Solutions**:
```python
import uasyncio as asyncio
from primitives import Queue

# 1. Ensure process_callbacks() is being called
async def callback_processor():
    while True:
        core1.process_callbacks()
        await asyncio.sleep_ms(10)

asyncio.create_task(callback_processor())

# 2. Use larger queue
result_queue = Queue(maxsize=20)  # Default is often 5

# 3. Consume promptly
async def worker():
    while True:
        event = await result_queue.get()
        result = event.get_result()
        # Process immediately
        print(f"Got result: {result}")

# 4. Check for retry warnings
# If you see "Failed to put event in queue after 10 retries"
# it means the queue is consistently full
```

**Warning Signs**:
```
Warning: Failed to put event in queue after 10 retries
```
This means:
- Queue is staying full
- Consumer isn't keeping up
- Need larger queue or faster consumer

### Race Conditions

**Symptom**: Intermittent failures, especially when switching between modes

**Cause**: Residual state from previous operations

**Solution**:
```python
# Clear state between mode switches
core1.process_callbacks()  # Clear callback queue
time.sleep(0.1)  # Let monitor process any pending

# Now start new mode
event = core1.call_event(...)
```

### GIL-Related Crashes

**Symptom**: System crash, especially with callbacks

**Cause**: Creating Python objects in wrong thread context

**Note**: This should not happen with the current implementation. If it does:
- You've modified the C code
- There's a bug - please report it!

**Protection Built-In**:
- Monitor task stores **raw response data**
- Python objects created only in `process_callbacks()`
- All callbacks executed in MicroPython thread

---

## Advanced Topics

### Adding Custom Commands

1. **Define command ID** in `core1_api.h`:
```c
typedef enum {
    CMD_ECHO = 0x0001,
    CMD_ADD = 0x0002,
    CMD_MY_CUSTOM = 0x0100,  // Your command
} core1_command_id_t;
```

2. **Implement handler** in `core1_api.c`:
```c
case CMD_MY_CUSTOM: {
    // Extract input from cmd.payload
    uint32_t input = *(uint32_t*)&cmd.payload[0];
    
    // Perform operation
    uint32_t output = process(input);
    
    // Store result in resp.payload
    memcpy(resp.payload, &output, sizeof(output));
    break;
}
```

3. **Export constant** in `modcore1.c`:
```c
{ MP_ROM_QSTR(MP_QSTR_CMD_MY_CUSTOM), MP_ROM_INT(CMD_MY_CUSTOM) },
```

4. **Use from Python**:
```python
result = core1.call(cmd_id=core1.CMD_MY_CUSTOM, data=struct.pack('I', 42))
output = struct.unpack('I', result[:4])[0]
```

### Configuration Tuning

Edit `core1_api.h`:

```c
// Queue sizes (memory vs. capacity trade-off)
#define CORE1_CMD_QUEUE_SIZE 16      // Increase for more buffering
#define CORE1_RESP_QUEUE_SIZE 16
#define CORE1_MAX_PENDING 32         // Max concurrent operations

// Task configuration
#define CORE1_TASK_STACK_SIZE 4096   // Increase if operations need more stack
#define CORE1_TASK_PRIORITY 5        // Higher = more responsive

// Payload size
#define CORE1_MAX_PAYLOAD_SIZE 128   // Increase for larger data transfers
```

### Debug Logging

Enable ESP-IDF logging from Python:

```python
core1.set_log_level(3)  # 0=NONE, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=VERBOSE
```

View logs on USB/JTAG serial port or with:
```bash
make BOARD=BDARS_S3 monitor
```

---

## Conclusion

The Core1 API provides a robust, safe mechanism for dual-core processing in MicroPython on ESP32-S3. Key takeaways:

✅ **Three modes** for different use cases
✅ **Thread-safe** by design
✅ **Easy to use** with sensible defaults
✅ **Extensible** for custom commands
✅ **Production-ready** after extensive testing

For most applications, start with **blocking mode** for simplicity, then migrate to **callback** or **event** mode when you need concurrency.

Happy dual-core programming! 🚀
