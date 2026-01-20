import socket


class ZwoClient:
    def __init__(self, ip_address: str = "127.0.0.1", port: int = 52311):
        self.ip_address = ip_address
        self.port = port
        self.socket = None
        self._connected = False

    def connect(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.connect((self.ip_address, self.port))
        self._connected = True

    def close(self):
        if self.socket:
            self.socket.close()
            self.socket = None
        self._connected = False

    def send(self, command: str):
        if not self._connected:
            raise ConnectionError("Client is not connected.")
        command += "\n"
        self.socket.sendall(command.encode("utf-8"))
        return self.socket.recv(1024).decode("utf-8").strip()

    def open(self):
        if not self._connected:
            self.connect()
        self.send("open\n")

    def get_temp(self) -> float:
        if not self._connected:
            raise ConnectionError("Client is not connected.")
        response = self.send("tempcon\n").split()
        return [float(x) for x in response if x.replace(".", "", 1).isdigit()]
    
    def fan_on(self):
        if not self._connected:
            raise ConnectionError("Client is not connected.")
        return self.send("fancon on\n")
    
    def fan_off(self):
        if not self._connected:
            raise ConnectionError("Client is not connected.")
        return self.send("fancon off\n")

