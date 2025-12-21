import sys
import pyboard

def connect_to_device(device):
    """Connect to a MicroPython device via serial, telnet, or RFC2217"""
    pyb = None
    try:
        # Connect using pyboard - supports serial ports, IP addresses, and RFC2217 URLs
        print(f"Connecting to {device}...")
        pyb = pyboard.Pyboard(device)
        print(f"Connected to {device}")
        
        # Enter raw REPL mode for reliable communication
        pyb.enter_raw_repl()
        
        # Interactive REPL loop
        while True:
            try:
                line = sys.stdin.readline()
                if not line:
                    break
                
                # Send command and get response
                result = pyb.exec_(line.strip())
                if result:
                    sys.stdout.write(result.decode('utf-8', errors='replace'))
                    sys.stdout.flush()
                    
            except Exception as e:
                print(f"Error executing command: {e}")
                break
    
    except KeyboardInterrupt:
        print("\nDisconnected")
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
    finally:
        if pyb:
            try:
                pyb.exit_raw_repl()
                pyb.close()
            except:
                pass

if __name__ == '__main__':
    # Accept device as command line argument or use default
    device = sys.argv[1] if len(sys.argv) > 1 else 'rfc2217://localhost:4000'
    connect_to_device(device)