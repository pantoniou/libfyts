#include <fyts/fyts.h>

int main(void)
{
	if (!fyts_language_supported("c"))
		return 1;
	if (!fyts_language_supported("python"))
		return 1;
	if (fyts_language_supported("not-a-language"))
		return 1;
	if (fyts_language_supported(""))
		return 1;
	if (fyts_language_supported(NULL))
		return 1;
	return 0;
}
