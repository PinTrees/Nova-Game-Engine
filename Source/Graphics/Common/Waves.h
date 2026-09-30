#pragma once


class Waves
{
public:
	Waves();
	~Waves();

	uint32 RowCount() { return _numRows; }
	uint32 ColumnCount() { return _numCols; }
	uint32 VertexCount() { return _vertexCount; }
	uint32 TriangleCount() { return _triangleCount; }
	
	float Width() {	return _numCols * _spatialStep; }
	float Depth() {	return _numRows * _spatialStep; }

	// Returns the solution at the ith grid point.
	const XMFLOAT3& operator[](int32 i) const { return _currSolution[i]; }
	// Returns the solution normal at the ith grid point.
	const XMFLOAT3& Normal(int32 i)const { return _normals[i]; }
	// Returns the unit tangent vector at the ith grid point in the local x-axis direction.
	const XMFLOAT3& TangentX(int32 i)const { return _tangentX[i]; }

	void Init(uint32 m, uint32 n, float dx, float dt, float speed, float damping);
	void Update(float dt);
	void Disturb(uint32 i, uint32 j, float magnitude);

private:
	uint32 _numRows = 0;
	uint32 _numCols = 0;
	uint32 _vertexCount = 0;
	uint32 _triangleCount = 0;

	// Simulation constants we can precompute.
	float _k1 = 0.f;
	float _k2 = 0.f;
	float _k3 = 0.f;
	float _timeStep = 0.f;
	float _spatialStep = 0.f;

	vector<XMFLOAT3> _prevSolution;
	vector<XMFLOAT3> _currSolution;
	vector<XMFLOAT3> _normals;
	vector<XMFLOAT3> _tangentX;
};

