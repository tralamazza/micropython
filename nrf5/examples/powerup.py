# This file is part of the Micro Python project, http://micropython.org/
#
# The MIT License (MIT)
#
# Copyright (c) 2017 Glenn Ruben Bakke
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE

from machine import ADC
from machine import Pin

class PowerUp3:
    def __init__(self):
        self.x_adc = ADC(1)
        self.y_adc = ADC(2)
        self.x_mid = 0
        self.y_mid = 0
        
        self.calibrate()
        
    def read_stick_x(self):
        return self.x_adc.value()
        
    def read_stick_y(self):
        return self.y_adc.value()
        
    def calibrate(self):
        self.x_mid = self.read_stick_x()
        self.y_mid = self.read_stick_y()
        
    def __str__(self):
        return "calibration x: %i, y: %i" % (self.x_mid, self.y_mid)
