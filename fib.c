#include<stdio.h>
#include<stdlib.h>

int fib(int n) {
  if(n<2) {return n;}
  else return fib(n-1)+fib(n-2);
}

int main(int argc, char **argv) {
	int val = fib(40);
	printf("Value: %d \n",val);
	return 0;
}
