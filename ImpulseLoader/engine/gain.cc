// generated from file '/home/brummer/projecte/PreAmpImpulses/gain.dsp' by dsp2cc:
// Code generated with Faust 2.54.9 (https://faust.grame.fr)

#include <cmath>

namespace gain {

class Dsp {
private:
	uint32_t fSampleRate;
	double fRec0[2];

public:
	float gain;
	void del_instance(Dsp *p);
	void clear_state_f();
	void init(uint32_t sample_rate);
	void compute(int count, float *input0, float *output0);
	Dsp();
	~Dsp();
};

Dsp::Dsp() {
	gain = 0.0;
}

Dsp::~Dsp() {
}

inline void Dsp::clear_state_f()
{
	for (int l0 = 0; l0 < 2; l0 = l0 + 1) fRec0[l0] = 0.0;
}

inline void Dsp::init(uint32_t sample_rate)
{
	fSampleRate = sample_rate;
	clear_state_f();
}

void Dsp::compute(int count, float *input0, float *output0)
{
	float fSlow0 = 0.0010000000000000009 * std::pow(1e+01, 0.05 * gain);
	for (int i0 = 0; i0 < count; i0 = i0 + 1) {
		fRec0[0] = fSlow0 + 0.999 * fRec0[1];
		output0[i0] = input0[i0] * fRec0[0];
		fRec0[1] = fRec0[0];
	}
}

Dsp *plugin() {
	return new Dsp();
}

void Dsp::del_instance(Dsp *p)
{
	delete p;
}
} // end namespace gain
