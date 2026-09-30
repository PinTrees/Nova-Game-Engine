#pragma once

class Camera;
class ParticleEffect;

class ParticleSystem
{
public:

	ParticleSystem();
	~ParticleSystem();

	// Time elapsed since the system was reset.
	float GetAge() const;

	void SetEyePos(const XMFLOAT3& eyePosW);
	void SetEmitPos(const XMFLOAT3& emitPosW);
	void SetEmitDir(const XMFLOAT3& emitDirW);

	void Init(ComPtr<ID3D11Device> device, shared_ptr<ParticleEffect> fx,
		ComPtr<ID3D11ShaderResourceView> texArraySRV,
		ComPtr<ID3D11ShaderResourceView> randomTexSRV,
		uint32 maxParticles);

	void Reset();
	void Update(float dt, float gameTime);
	void Draw(ComPtr<ID3D11DeviceContext> dc, const Camera& cam);

private:
	void BuildVB(ComPtr<ID3D11Device> device);

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

	ComPtr<ID3D11Buffer> _initVB;
	ComPtr<ID3D11Buffer> _drawVB;
	ComPtr<ID3D11Buffer> _streamOutVB;

	ComPtr<ID3D11ShaderResourceView> _texArraySRV;
	ComPtr<ID3D11ShaderResourceView> _randomTexSRV;
};
