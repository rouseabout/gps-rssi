#include <stdint.h>
#include <stdio.h>

int main(void)
{
	while(1)
	{
		int8_t buf;
		if(fread(&buf, sizeof(buf), 1, stdin) != 1)
			break;

		float out = buf / 128.0f;
		fwrite(&out, sizeof(out), 1, stdout);
	}
	return 0;
}
