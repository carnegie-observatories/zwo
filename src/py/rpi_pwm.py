# sudo apt-get install python3-pigpio python3-gpiozero
from gpiozero import PWMLED, Servo
from time import sleep
from gpiozero.tools import scaled, sin_values

from gpiozero.pins.pigpio import PiGPIOFactory

factory = PiGPIOFactory(host='10.8.80.225')

servo = Servo(13, pin_factory=factory)

servo.source = scaled(sin_values(), -0.03, 0.03)
servo.source_delay = 0.1

# Define the LED on GPIO pin 17
led = PWMLED(12, pin_factory=factory)
max_led = 30
interval = 1

while True:
    # Fade in (increase value from 0 to 1)
    for brightness in range(0, max_led+1, interval):
        led.value = brightness / 100.0
        sleep(0.1)
    # Fade out (decrease value from 1 to 0)
    for brightness in range(max_led, -1, -interval):
        led.value = brightness / 100.0
        sleep(0.1)
