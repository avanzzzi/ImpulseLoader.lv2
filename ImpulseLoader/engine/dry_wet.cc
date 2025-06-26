// generated from file '/home/brummer/projecte/XUiDesigner/tools/wet_dry.dsp' by dsp2cc:
// Code generated with Faust 2.69.3 (https://faust.grame.fr)

#include <cmath>


namespace wet_dry {

class Dsp {
private:
	uint32_t fSampleRate;
	float fVslider0;
	float	*fVslider0_;


public:
	float dry_wet;
	void connect(uint32_t port,void* data);
	void del_instance(Dsp *p);
	void init(uint32_t sample_rate);
	void compute(int count, float *input0, float *input1, float *output0);
	Dsp();
	~Dsp();
};



Dsp::Dsp() {
	dry_wet = 100.0;
}

Dsp::~Dsp() {
}

inline void Dsp::init(uint32_t sample_rate)
{
	fSampleRate = sample_rate;
}

void Dsp::compute(int count, float *input0, float *input1, float *output0)
{
	float fSlow0 = 0.01f * dry_wet;
	float fSlow1 = 1.0f - fSlow0;
	for (int i0 = 0; i0 < count; i0 = i0 + 1) {
		output0[i0] = fSlow1 * input0[i0] + fSlow0 * input1[i0];
	}
}


Dsp *plugin() {
	return new Dsp();
}

void Dsp::del_instance(Dsp *p)
{
	delete p;
}
} // end namespace wet_dry
