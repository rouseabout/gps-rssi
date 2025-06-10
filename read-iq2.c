#include <stdint.h>
#include <stdio.h>

int main(void)
{
	while(1)
	{
		int8_t buf;
		if(fread(&buf, sizeof(buf), 1, stdin) != 1)
			break;

#define _(TheByte, h, l) \
{ \
        int IMag1 = (TheByte >> (h)) & 1; \
        int ISign1 = (TheByte >> (l)) & 1; \
        float I1 = (IMag1 == 1) ? 1.0 : 1.0/3.0; \
        I1 = (ISign1 == 1) ? -I1 : I1; \
        fwrite(&I1, sizeof(I1), 1, stdout); \
}
                _(buf, 7, 6)
                _(buf, 5, 4)
                _(buf, 3, 2)
                _(buf, 1, 0)

		float out = buf / 32768.0f;
		fwrite(&out, sizeof out, 1, stdout);
	}
	return 0;
}
