from Polygon import *
from Polygon.Shapes import Circle, Star, Rectangle
from Polygon.IO import writeSVG

p =  Circle(5.5, points=128)
p -= Circle(4.4, points=128)
p += Circle(3.3, points=128) 
p -= Circle(2.2, points=128) 
p += Star(1.1)

l = Rectangle(4, 14)
l.shift(-4.3, -10.5)

r = p ^ l

writeSVG('logo.svg', [r], width=800)
