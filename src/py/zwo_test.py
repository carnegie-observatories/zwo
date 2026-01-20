#!/usr/bin/env python3
"""
ZWO Camera Protocol Test Suite

This script tests and compares the ZWO emulator with a real camera server.
It validates that the emulator correctly implements the protocol.

Usage:
    # Test emulator only:
    python zwo_test.py --emulator-only
    
    # Compare emulator with real camera on localhost:52311:
    python zwo_test.py --real-host localhost --real-port 52311
    
    # Compare with custom ports:
    python zwo_test.py --emulator-port 52312 --real-port 52311
"""

import socket
import time
import argparse
import sys
from dataclasses import dataclass
from typing import Optional, Tuple, List
import numpy as np

# Import the emulator
from zwo_emulator import ZwoEmulator


@dataclass
class TestResult:
    """Result of a single test."""
    name: str
    passed: bool
    message: str
    emulator_response: Optional[str] = None
    real_response: Optional[str] = None


class ZwoTestClient:
    """Test client for ZWO camera server."""
    
    def __init__(self, host: str, port: int, name: str = ""):
        self.host = host
        self.port = port
        self.name = name or f"{host}:{port}"
        self.socket: Optional[socket.socket] = None
        
    def connect(self) -> bool:
        """Connect to the server."""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(5.0)
            self.socket.connect((self.host, self.port))
            return True
        except Exception as e:
            print(f"[{self.name}] Connection failed: {e}")
            return False
            
    def disconnect(self):
        """Disconnect from the server."""
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
            
    def send_command(self, command: str, expect_binary: int = 0) -> Tuple[str, Optional[bytes]]:
        """
        Send a command and receive response.
        
        Args:
            command: The command to send
            expect_binary: Number of binary bytes to expect after text response (0 = none)
            
        Returns:
            Tuple of (text_response, binary_data or None)
        """
        if not self.socket:
            raise ConnectionError("Not connected")
            
        # Send command
        self.socket.sendall((command + "\n").encode("utf-8"))
        
        # Receive text response (until newline)
        response = b""
        while True:
            chunk = self.socket.recv(1)
            if not chunk:
                break
            if chunk == b"\n":
                break
            response += chunk
            
        text_response = response.decode("utf-8", errors="ignore")
        
        # Receive binary data if expected
        binary_data = None
        if expect_binary > 0:
            binary_data = b""
            while len(binary_data) < expect_binary:
                remaining = expect_binary - len(binary_data)
                chunk = self.socket.recv(min(8192, remaining))
                if not chunk:
                    break
                binary_data += chunk
                
        return text_response, binary_data
        
    def __enter__(self):
        self.connect()
        return self
        
    def __exit__(self, *args):
        self.disconnect()


class ZwoProtocolTester:
    """Tests ZWO camera protocol implementations."""
    
    def __init__(self, emulator_port: int = 52312, 
                 real_host: Optional[str] = None, 
                 real_port: int = 52311):
        self.emulator_port = emulator_port
        self.real_host = real_host
        self.real_port = real_port
        self.emulator: Optional[ZwoEmulator] = None
        self.results: List[TestResult] = []
        
    def start_emulator(self):
        """Start the emulator server."""
        self.emulator = ZwoEmulator(port=self.emulator_port, random_seed=42)
        self.emulator.start(blocking=False)
        time.sleep(0.5)  # Wait for server to start
        
    def stop_emulator(self):
        """Stop the emulator server."""
        if self.emulator:
            self.emulator.stop()
            
    def test_command(self, name: str, command: str, 
                     expect_binary: int = 0,
                     validate_func=None) -> TestResult:
        """
        Test a command against both emulator and optionally the real server.
        
        Args:
            name: Test name
            command: Command to send
            expect_binary: Expected binary bytes
            validate_func: Optional function(response) -> bool to validate response
        """
        emu_response = None
        real_response = None
        emu_binary = None
        real_binary = None
        passed = True
        message = ""
        
        # Test emulator
        try:
            with ZwoTestClient("localhost", self.emulator_port, "emulator") as client:
                emu_response, emu_binary = client.send_command(command, expect_binary)
        except Exception as e:
            passed = False
            message = f"Emulator error: {e}"
            
        # Test real server if available
        if self.real_host and passed:
            try:
                with ZwoTestClient(self.real_host, self.real_port, "real") as client:
                    real_response, real_binary = client.send_command(command, expect_binary)
            except Exception as e:
                message = f"Real server error (non-fatal): {e}"
                
        # Validate responses
        if passed and validate_func:
            if not validate_func(emu_response):
                passed = False
                message = f"Validation failed for emulator: {emu_response}"
                
        # Compare responses if both available
        if passed and emu_response and real_response:
            # Parse and compare (format may differ slightly)
            emu_parts = emu_response.split()
            real_parts = real_response.split()
            
            # For some commands, just check structure matches
            if len(emu_parts) != len(real_parts):
                message = f"Response structure differs: emu={len(emu_parts)} parts, real={len(real_parts)} parts"
                
        result = TestResult(
            name=name,
            passed=passed,
            message=message,
            emulator_response=emu_response,
            real_response=real_response
        )
        self.results.append(result)
        return result
        
    def run_protocol_tests(self):
        """Run a sequence of protocol tests."""
        print("=" * 60)
        print("ZWO Protocol Test Suite")
        print("=" * 60)
        
        # Start emulator
        self.start_emulator()
        
        try:
            # Connect and run tests
            print("\n[Test Sequence]\n")
            
            # Test 1: Version command
            print("1. Testing 'version' command...")
            with ZwoTestClient("localhost", self.emulator_port, "emulator") as emu_client:
                resp, _ = emu_client.send_command("version")
                parts = resp.split()
                passed = len(parts) >= 3
                print(f"   Emulator: {resp}")
                print(f"   Passed: {passed} (expects: version cookie timestamp)")
                self.results.append(TestResult("version", passed, "", resp))
                
            # Test 2: Open camera
            print("\n2. Testing 'open' command...")
            with ZwoTestClient("localhost", self.emulator_port, "emulator") as emu_client:
                resp, _ = emu_client.send_command("open")
                parts = resp.split()
                passed = len(parts) >= 6
                print(f"   Emulator: {resp}")
                print(f"   Passed: {passed} (expects: width height cooler color bitDepth model)")
                self.results.append(TestResult("open", passed, "", resp))
                
                # Test 3: Status
                print("\n3. Testing 'status' command...")
                resp, _ = emu_client.send_command("status")
                passed = resp in ["idle", "closed", "exposing", "streaming"]
                print(f"   Emulator: {resp}")
                print(f"   Passed: {passed}")
                self.results.append(TestResult("status", passed, "", resp))
                
                # Test 4: Setup
                print("\n4. Testing 'setup' command...")
                resp, _ = emu_client.send_command("setup 0 0 256 256 1 16")
                parts = resp.split()
                passed = len(parts) == 6
                print(f"   Emulator: {resp}")
                print(f"   Passed: {passed} (expects: x y w h bin bits)")
                self.results.append(TestResult("setup", passed, "", resp))
                
                # Test 5: Exptime
                print("\n5. Testing 'exptime' command...")
                resp, _ = emu_client.send_command("exptime 0.05")
                try:
                    exp = float(resp)
                    passed = abs(exp - 0.05) < 0.001
                except:
                    passed = False
                print(f"   Emulator: {resp}")
                print(f"   Passed: {passed}")
                self.results.append(TestResult("exptime", passed, "", resp))
                
                # Test 6: Gain
                print("\n6. Testing 'gain' command...")
                resp, _ = emu_client.send_command("gain 100")
                try:
                    gain = int(resp)
                    passed = gain == 100
                except:
                    passed = False
                print(f"   Emulator: {resp}")
                print(f"   Passed: {passed}")
                self.results.append(TestResult("gain", passed, "", resp))
                
                # Test 7: Offset
                print("\n7. Testing 'offset' command...")
                resp, _ = emu_client.send_command("offset 20")
                try:
                    offset = int(resp)
                    passed = offset == 20
                except:
                    passed = False
                print(f"   Emulator: {resp}")
                print(f"   Passed: {passed}")
                self.results.append(TestResult("offset", passed, "", resp))
                
                # Test 8: Tempcon
                print("\n8. Testing 'tempcon' command...")
                resp, _ = emu_client.send_command("tempcon")
                parts = resp.split()
                passed = len(parts) == 2
                print(f"   Emulator: {resp}")
                print(f"   Passed: {passed} (expects: temp power)")
                self.results.append(TestResult("tempcon", passed, "", resp))
                
                # Test 9: Expose and data
                print("\n9. Testing 'expose' + 'data' sequence...")
                resp, _ = emu_client.send_command("expose")
                print(f"   Expose: {resp}")
                
                time.sleep(0.1)  # Wait for exposure
                
                resp, _ = emu_client.send_command("status")
                print(f"   Status: {resp}")
                
                resp, _ = emu_client.send_command("data")
                print(f"   Data response: {resp}")
                
                try:
                    size = int(resp)
                    expected_size = 256 * 256 * 2  # 16-bit
                    passed = size == expected_size
                    print(f"   Expected size: {expected_size}, Got: {size}")
                except:
                    passed = False
                    size = 0
                    
                # Read binary data
                if size > 0:
                    binary_data = b""
                    while len(binary_data) < size:
                        chunk = emu_client.socket.recv(min(8192, size - len(binary_data)))
                        if not chunk:
                            break
                        binary_data += chunk
                    print(f"   Received {len(binary_data)} bytes")
                    
                    # Verify data format
                    if len(binary_data) == size:
                        image = np.frombuffer(binary_data, dtype=np.uint16).reshape((256, 256))
                        print(f"   Image stats: min={image.min()}, max={image.max()}, mean={image.mean():.1f}")
                        passed = passed and (image.max() > image.min())
                        
                print(f"   Passed: {passed}")
                self.results.append(TestResult("expose+data", passed, "", resp))
                
                # Test 10: Filter wheel
                print("\n10. Testing 'filters' and 'filter' commands...")
                resp, _ = emu_client.send_command("filters")
                try:
                    count = int(resp)
                    passed = count > 0
                except:
                    passed = False
                print(f"   Filter count: {resp}")
                
                resp, _ = emu_client.send_command("filter 3")
                try:
                    pos = int(resp)
                    passed = passed and (pos == 3)
                except:
                    passed = False
                print(f"   Filter position: {resp}")
                print(f"   Passed: {passed}")
                self.results.append(TestResult("filters", passed, "", resp))
                
                # Test 11: Close
                print("\n11. Testing 'close' command...")
                resp, _ = emu_client.send_command("close")
                passed = resp == "OK"
                print(f"   Emulator: {resp}")
                print(f"   Passed: {passed}")
                self.results.append(TestResult("close", passed, "", resp))
                
        finally:
            self.stop_emulator()
            
        # Print summary
        self.print_summary()
        
    def run_comparison_test(self):
        """Run comparison tests between emulator and real server."""
        if not self.real_host:
            print("No real server specified for comparison")
            return
            
        print("=" * 60)
        print("ZWO Emulator vs Real Server Comparison")
        print("=" * 60)
        print(f"Emulator: localhost:{self.emulator_port}")
        print(f"Real:     {self.real_host}:{self.real_port}")
        print("=" * 60)
        
        # Start emulator
        self.start_emulator()
        
        try:
            # Test commands on both
            commands_to_test = [
                ("version", 0),
                ("open", 0),
                ("status", 0),
                ("setup image 2", 0),
                ("exptime 0.01", 0),
                ("gain 50", 0),
                ("offset 10", 0),
                ("tempcon", 0),
            ]
            
            print("\n[Command Comparison]\n")
            print(f"{'Command':<25} {'Emulator':<30} {'Real Server':<30}")
            print("-" * 85)
            
            emu_client = ZwoTestClient("localhost", self.emulator_port, "emulator")
            real_client = ZwoTestClient(self.real_host, self.real_port, "real")
            
            emu_connected = emu_client.connect()
            real_connected = real_client.connect()
            
            if not emu_connected:
                print("Failed to connect to emulator")
                return
            if not real_connected:
                print("Failed to connect to real server")
                
            for cmd, binary_size in commands_to_test:
                emu_resp = "N/A"
                real_resp = "N/A"
                
                try:
                    emu_resp, _ = emu_client.send_command(cmd, binary_size)
                except Exception as e:
                    emu_resp = f"Error: {e}"
                    
                if real_connected:
                    try:
                        real_resp, _ = real_client.send_command(cmd, binary_size)
                    except Exception as e:
                        real_resp = f"Error: {e}"
                        
                print(f"{cmd:<25} {emu_resp[:28]:<30} {real_resp[:28]:<30}")
                
            # Test image capture comparison
            print("\n[Image Capture Comparison]\n")
            
            # Emulator capture
            print("Emulator image capture:")
            try:
                emu_client.send_command("expose")
                time.sleep(0.05)
                resp, _ = emu_client.send_command("data")
                emu_size = int(resp)
                emu_data = b""
                while len(emu_data) < emu_size:
                    chunk = emu_client.socket.recv(min(8192, emu_size - len(emu_data)))
                    if not chunk:
                        break
                    emu_data += chunk
                print(f"  Size: {len(emu_data)} bytes")
                if emu_data:
                    emu_image = np.frombuffer(emu_data, dtype=np.uint16)
                    print(f"  Stats: min={emu_image.min()}, max={emu_image.max()}, mean={emu_image.mean():.1f}")
            except Exception as e:
                print(f"  Error: {e}")
                
            # Real capture
            if real_connected:
                print("Real server image capture:")
                try:
                    real_client.send_command("expose")
                    time.sleep(0.05)
                    resp, _ = real_client.send_command("data")
                    real_size = int(resp)
                    real_data = b""
                    while len(real_data) < real_size:
                        chunk = real_client.socket.recv(min(8192, real_size - len(real_data)))
                        if not chunk:
                            break
                        real_data += chunk
                    print(f"  Size: {len(real_data)} bytes")
                    if real_data:
                        real_image = np.frombuffer(real_data, dtype=np.uint16)
                        print(f"  Stats: min={real_image.min()}, max={real_image.max()}, mean={real_image.mean():.1f}")
                except Exception as e:
                    print(f"  Error: {e}")
                    
            # Cleanup
            try:
                emu_client.send_command("close")
            except:
                pass
            if real_connected:
                try:
                    real_client.send_command("close")
                except:
                    pass
                    
            emu_client.disconnect()
            real_client.disconnect()
            
        finally:
            self.stop_emulator()
            
    def print_summary(self):
        """Print test summary."""
        print("\n" + "=" * 60)
        print("Test Summary")
        print("=" * 60)
        
        passed = sum(1 for r in self.results if r.passed)
        total = len(self.results)
        
        for result in self.results:
            status = "✓ PASS" if result.passed else "✗ FAIL"
            print(f"  {status}: {result.name}")
            if result.message:
                print(f"         {result.message}")
                
        print("-" * 60)
        print(f"Total: {passed}/{total} tests passed")
        print("=" * 60)


def main():
    parser = argparse.ArgumentParser(description="ZWO Protocol Test Suite")
    parser.add_argument("--emulator-port", type=int, default=52312,
                        help="Port for emulator (default: 52312)")
    parser.add_argument("--real-host", type=str, default=None,
                        help="Host of real camera server")
    parser.add_argument("--real-port", type=int, default=52311,
                        help="Port of real camera server (default: 52311)")
    parser.add_argument("--emulator-only", action="store_true",
                        help="Only test emulator, no comparison")
    parser.add_argument("--compare", action="store_true",
                        help="Run comparison between emulator and real server")
    
    args = parser.parse_args()
    
    tester = ZwoProtocolTester(
        emulator_port=args.emulator_port,
        real_host=args.real_host if not args.emulator_only else None,
        real_port=args.real_port
    )
    
    if args.compare and args.real_host:
        tester.run_comparison_test()
    else:
        tester.run_protocol_tests()
        
    # Return exit code based on results
    failed = sum(1 for r in tester.results if not r.passed)
    sys.exit(min(failed, 127))


if __name__ == "__main__":
    main()
