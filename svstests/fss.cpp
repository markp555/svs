#include <filesubsystem.h>
#include <svslib.h>

void fss_test()
{
	int k = sizeof(svs::filesave);
	svs::SVS vs(L"a.db", L"a.dat", NULL, false, false);
	std::string file1 = "Hello, World!", file2, file3;
	for (size_t i = 0; i < 1000; i++)
	{
		for (size_t j = 0; j < 26; j++)
		{
			file2 += (char)('A' + j);
		}
	}
	file3 = "Hello, " + file2 + "!!!";
	char end = '\0';
	long long a, b, c, d;
	{
		svs::filesave fsv(vs.acquire_data(), vs.acquire_db(), file1.size() + 1, 0);
		fsv.process(file1.c_str(), file1.size());
		fsv.process(&end, 1);
		a = fsv.commit(NULL);
	}
	{
		svs::filesave fsv2(vs.acquire_data(), vs.acquire_db(), file2.size() + 1, 0);
		fsv2.process(file2.c_str(), file2.size());
		fsv2.process(&end, 1);
		b = fsv2.commit(NULL);
	}
	{
		svs::filesave fsv3(vs.acquire_data(), vs.acquire_db(), file3.size() + 1, b);
		fsv3.process(file3.c_str(), file3.size());
		fsv3.process(&end, 1);
		c = fsv3.commit(NULL);
	}
	file1 += "?";
	{
		svs::filesave fsv(vs.acquire_data(), vs.acquire_db(), file1.size() + 1, 0);
		fsv.process(file1.c_str(), file1.size());
		fsv.process(&end, 1);
		d = fsv.commit(NULL);
	}
	printf("%lld %lld %lld %lld\n", a, b, c, d);
}