/*
 * grus builds this shim with clang (grus/tools/shimbuild.py). gcc expands
 * __builtin_strcmp against string literals inline; clang emits a call,
 * which a freestanding image has to provide.
 */
int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}
