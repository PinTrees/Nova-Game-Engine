#pragma once

class Camera;
class ParticleEffect;

class StreamOutParticles
{
public:

	StreamOutParticles();
	~StreamOutParticles();

	// Time elapsed since the system was reset.
	float GetAge() const;

	void SetEyePos(const XMFLOAT3& eyePosW);
	void SetEmitPos(const XMFLOAT3& emitPosW);
	void SetEmitDir(const XMFLOAT3& emitDirW);

	void Init(ComPtr<GfxDevice> device, shared_ptr<ParticleEffect> fx,
		ComPtr<GfxShaderResourceView> texArraySRV,
		ComPtr<GfxShaderResourceView> randomTexSRV,
		uint32 maxParticles);

	void Reset();
	void Update(float dt, float gameTime);
	void Draw(ComPtr<GfxContext> dc, const Camera& cam);

private:
	void BuildVB(ComPtr<GfxDevice> device);

private:
	uint32 _maxParticles = 0;
	bool _firstRun;

	float _gameTime;
	float _timeStep;
	float _age;

	XMFLOAT3 _eyePosW;
	XMFLOAT3 _emitPosW;
	XMFLOAT3 _emitDirW;

	shared_ptr<class ParticleEffect> _fx;

	ComPtr<GfxBuffer> _initVB;
	ComPtr<GfxBuffer> _drawVB;
	ComPtr<GfxBuffer> _streamOutVB;

	ComPtr<GfxShaderResourceView> _texArraySRV;
	ComPtr<GfxShaderResourceView> _randomTexSRV;
};
