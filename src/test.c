int testme(int a, int b) {
	if (a > b) {
		a = 2*a;
	}
	else {
		b = 3*b;
	}
	return a + b;
}

int testme2(int a, int b) {
	return testme(a, b);
}

int testme3(int a, int b) {
	if (a < b) {
		a = 2*a;
	}
	else {
		b = 3*b;
	}
	return a + b;
}
