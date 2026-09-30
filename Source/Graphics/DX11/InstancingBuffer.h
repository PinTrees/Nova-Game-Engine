#pragma once

struct InstancingData
{
	XMMATRIX world;
};

#define MAX_MESH_INSTANCE 500

class InstancingBuffer
{
public:
	InstancingBuffer();
	~InstancingBuffer();

private:
	void CreateBuffer(uint32 maxCount = MAX_MESH_INSTANCE);

	template<typename T>
	void Create(ComPtr<ID3D11Device> device, const vector<T>& vertices, uint32 slot = 0, bool cpuWrite = false, bool gpuWrite = false)
	{
		_stride = sizeof(T);
		//_count = static_cast<uint32>(vertices.size()); // == _maxCount

		_slot = slot;
		_cpuWrite = cpuWrite;
		_gpuWrite = gpuWrite;

		D3D11_BUFFER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.ByteWidth = (uint32)(_stride * _maxCount);

		if (cpuWrite == false && gpuWrite == false)
		{
			desc.Usage = D3D11_USAGE_IMMUTABLE; // CPU Read, GPU Read
		}
		else if (cpuWrite == true && gpuWrite == false)
		{
			desc.Usage = D3D11_USAGE_DYNAMIC; // CPU Write, GPU Read
			desc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
		}
		else if (cpuWrite == false && gpuWrite == true) // CPU Read, GPU Write
		{
			desc.Usage = D3D11_USAGE_DEFAULT;
		}
		else
		{
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		}

		D3D11_SUBRESOURCE_DATA data;
		ZeroMemory(&data, sizeof(data));
		data.pSysMem = vertices.data();

		HRESULT hr = device->CreateBuffer(&desc, &data, _instanceBuffer.GetAddressOf());
		CHECK(hr);
	}

public:
	void ClearData();
	void AddData(InstancingData& data);

	void PushData(ComPtr<ID3D11DeviceContext> dc);

	ComPtr<ID3D11Buffer> GetComPtr() { return _instanceBuffer; }
	uint32 GetStride() { return _stride; }
	uint32 GetOffset() { return _offset; }
	//uint32 GetCount() { return _count; }
	uint32 GetSlot() { return _slot; }

public:
	uint32	GetCount() { return static_cast<uint32>(_data.size()); }

	void	SetID(uint64 instanceId) { _instanceId = instanceId; }
	uint64	GetID() { return _instanceId; }

private:
	uint64						_instanceId = 0;
	//shared_ptr<VertexBuffer>	_instanceBuffer;
	uint32						_maxCount = 0;
	vector<InstancingData>		_data;

	ComPtr<ID3D11Buffer> _instanceBuffer;

	uint32 _stride = 0;
	uint32 _offset = 0;
	//uint32 _count = 0;

	uint32 _slot = 0;
	bool _cpuWrite = false;
	bool _gpuWrite = false;
};

