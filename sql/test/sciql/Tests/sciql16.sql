CREATE ARRAY img (x int DIMENSION[0:1024:1], y int DIMENSION[0:1024:1], v float);
ALTER ARRAY img ALTER x DIMENSION[-5:*];
