#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <math.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "textfile.h"

#include "Vectors.h"
#include "Matrices.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#ifndef max
# define max(a,b) (((a)>(b))?(a):(b))
# define min(a,b) (((a)<(b))?(a):(b))
#endif

using namespace std;

// Default window size
int WINDOW_WIDTH = 800;
int WINDOW_HEIGHT = 800;

// Basic unit for angle degree
const float PI = 3.14;

// variables for mouse control
bool mouse_pressed = false;
int starting_press_x = -1;
int starting_press_y = -1;

enum TransMode
{
	GeoTranslation = 0,
	GeoRotation = 1,
	GeoScaling = 2,
	LightEdit = 3,
	ShininessEdit = 4,
};

enum ProjMode
{
	Orthogonal = 0,
	Perspective = 1,
};
ProjMode cur_proj_mode = Orthogonal;
TransMode cur_trans_mode = GeoTranslation;

// Shader attributes for uniform variables
GLuint iLocP; //projection matrix
GLuint iLocV; //viewing matrix
GLuint iLocModelMatrix; // T * R * S
GLuint iLocMV; // view_matrix * T * R * S;
GLuint iLocNormTrans; // MV.invert().transpose()

GLuint iLocLightIdx;

// material properties
GLuint iLocKa;
GLuint iLocKd;
GLuint iLocKs;
GLuint iLocShininess;

GLuint iLocVertex_or_perpixel;

struct Uniform
{
	GLint iLocMVP;
};
Uniform uniform;

// properties for light source in GPU
struct iLocLightInfo
{
	// lighting properties (general)
	GLuint position;
	GLuint ambient;
	GLuint diffuse;
	GLuint specular;

	// lighting properties (spot light)
	GLuint spotDirection;
	GLuint spotExponent;
	GLuint spotCutoff;

	// attenuation
	GLuint constantAttenuation;
	GLuint linearAttenuation;
	GLuint quadraticAttenuation;

}iLocLightInfo[3];

//CPU
struct LightInfo
{
	Vector4 position;
	Vector4 spotDirection;
	Vector4 ambient;
	Vector4 diffuse;
	Vector4 specular;
	float spotExponent;
	float spotCutoff;
	float constantAttenuation;
	float linearAttenuation;
	float quadraticAttenuation;
}lightInfo[3];

vector<string> filenames; // .obj filename list

struct PhongMaterial
{
	Vector3 Ka;
	Vector3 Kd;
	Vector3 Ks;
};

typedef struct
{
	GLuint vao;
	GLuint vbo;
	GLuint vboTex;
	GLuint ebo;
	GLuint p_color;
	int vertex_count;
	GLuint p_normal;
	PhongMaterial material;
	int indexCount;
	GLuint m_texture;
} Shape;

struct model
{
	Vector3 position = Vector3(0, 0, 0);
	Vector3 scale = Vector3(1, 1, 1);
	Vector3 rotation = Vector3(0, 0, 0);	// Euler form

	vector<Shape> shapes;
};
vector<model> models;

struct camera
{
	Vector3 position;
	Vector3 center;
	Vector3 up_vector;
};
camera main_camera;

struct project_setting
{
	GLfloat nearClip, farClip;
	GLfloat fovy;
	GLfloat aspect;
	GLfloat left, right, top, bottom;
};
project_setting proj;

int cur_idx = 0; // represent which model should be rendered now
int light_idx = 0;

Matrix4 view_matrix;
Matrix4 project_matrix;

// material property for specular
GLfloat shininess;

static GLvoid Normalize(GLfloat v[3])
{
	GLfloat l;

	l = (GLfloat)sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	v[0] /= l;
	v[1] /= l;
	v[2] /= l;
}

static GLvoid Cross(GLfloat u[3], GLfloat v[3], GLfloat n[3])
{

	n[0] = u[1] * v[2] - u[2] * v[1];
	n[1] = u[2] * v[0] - u[0] * v[2];
	n[2] = u[0] * v[1] - u[1] * v[0];
}

// [DO] given a translation vector then output a Matrix4 (Translation Matrix)
Matrix4 translate(Vector3 vec)
{
	return Matrix4(
		1, 0, 0, vec.x,
		0, 1, 0, vec.y,
		0, 0, 1, vec.z,
		0, 0, 0, 1
	);
}

// [DO] given a scaling vector then output a Matrix4 (Scaling Matrix)
Matrix4 scaling(Vector3 vec)
{
	return Matrix4(
		vec.x, 0, 0, 0,
		0, vec.y, 0, 0,
		0, 0, vec.z, 0,
		0, 0, 0, 1
	);
}


// [DO] given a float value then ouput a rotation matrix alone axis-X (rotate alone axis-X)
Matrix4 rotateX(GLfloat val)
{
	return  Matrix4(
		1, 0, 0, 0,
		0, cos(val), -sin(val), 0,
		0, sin(val), cos(val), 0,
		0, 0, 0, 1
	);
}

// [DO] given a float value then ouput a rotation matrix alone axis-Y (rotate alone axis-Y)
Matrix4 rotateY(GLfloat val)
{
	return Matrix4(
		cos(val), 0, sin(val), 0,
		0, 1, 0, 0,
		-sin(val), 0, cos(val), 0,
		0, 0, 0, 1
	);
}

// [DO] given a float value then ouput a rotation matrix alone axis-Z (rotate alone axis-Z)
Matrix4 rotateZ(GLfloat val)
{
	return Matrix4(
		cos(val), -sin(val), 0, 0,
		sin(val), cos(val), 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	);
}

Matrix4 rotate(Vector3 vec)
{
	return rotateX(vec.x)*rotateY(vec.y)*rotateZ(vec.z);
}

// [DO] compute viewing matrix accroding to the setting of main_camera
void setViewingMatrix()
{
	Vector3 P1P2 = main_camera.center - main_camera.position;
	Vector3 P1P3 = main_camera.up_vector;

	// Following fast viewing matrix derivation (Ch.6 p.72)
	Vector3 Rz = -P1P2.normalize();
	// cross product of two vectors
	Vector3 Rx = (P1P2.cross(P1P3)).normalize();
	Vector3 Ry = Rz.cross(Rx);

	//view_matrix = R * T (p.74)
	Matrix4 R = Matrix4(
		Rx[0], Rx[1], Rx[2], 0,
		Ry[0], Ry[1], Ry[2], 0,
		Rz[0], Rz[1], Rz[2], 0,
		0, 0, 0, 1
	);

	//T = translate(-main_camera.position);
	Matrix4 T = Matrix4(
		1, 0, 0, -main_camera.position[0],
		0, 1, 0, -main_camera.position[1],
		0, 0, 1, -main_camera.position[2],
		0, 0, 0, 1
	);

	view_matrix = R * T;
}

// [DO] compute persepective projection matrix
void setPerspective()
{
	float f = 1 / (tan(proj.fovy / 2 * PI / 180.0));
	project_matrix = Matrix4(
		f / proj.aspect, 0, 0, 0,
		0, f, 0, 0,
		0, 0, (proj.farClip + proj.nearClip) / (proj.nearClip - proj.farClip), (2 * proj.farClip * proj.nearClip) / (proj.nearClip - proj.farClip),
		0, 0, -1, 0
	);
}

// [TODO] compute orthogonal projection matrix
void setOrthogonal()
{
	//Parallel projection(Ch6 P.127)
	cur_proj_mode = Orthogonal;
	GLfloat RL = proj.right - proj.left;
	GLfloat TB = proj.top - proj.bottom;
	GLfloat FN = proj.farClip - proj.nearClip;
	GLfloat t_x = -(proj.right + proj.left) / RL;
	GLfloat t_y = -(proj.top + proj.bottom) / TB;
	GLfloat t_z = -(proj.nearClip + proj.farClip) / FN;

	project_matrix = Matrix4(
		2 / RL, 0, 0, t_x,
		0, 2 / TB, 0, t_y,
		0, 0, -2 / FN, t_z,
		0, 0, 0, 1
	);
}

void setGLMatrix(GLfloat* glm, Matrix4& m) {
	glm[0] = m[0];  glm[4] = m[1];   glm[8] = m[2];    glm[12] = m[3];
	glm[1] = m[4];  glm[5] = m[5];   glm[9] = m[6];    glm[13] = m[7];
	glm[2] = m[8];  glm[6] = m[9];   glm[10] = m[10];   glm[14] = m[11];
	glm[3] = m[12];  glm[7] = m[13];  glm[11] = m[14];   glm[15] = m[15];
}

// Call back function for window reshape
void ChangeSize(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
	// [TODO] change your aspect ratio
	proj.aspect = (float)width / (float)height;
	WINDOW_HEIGHT = height;
	WINDOW_WIDTH = width;
	setViewingMatrix();
	if (cur_proj_mode == Perspective) {
		setPerspective();
	}
	else {
		setOrthogonal();
	}
}

// set properties to uniform variable in shader
void setUniforms() {
	model cur_model = models[cur_idx];
	// [TODO] update translation, rotation and scaling
	Matrix4 T = translate(cur_model.position),
			R = rotate(cur_model.rotation),
			S = scaling(cur_model.scale);

	// matrix for shader, type: GLfloat
	GLfloat temp[16];

	// pass Model matrix 
	Matrix4 model_matrix = T * R * S;
	setGLMatrix(temp, model_matrix);
	glUniformMatrix4fv(iLocModelMatrix, 1, GL_FALSE, temp);

	// pass MV matrix
	Matrix4 MV = view_matrix * T * R * S;
	setGLMatrix(temp, MV); 
	glUniformMatrix4fv(iLocMV, 1, GL_FALSE, temp);

	// MVP = project_matrix * (view_matrix * T * R * S); 
	Matrix4 MVP = project_matrix * MV;
	setGLMatrix(temp, MVP); 
	glUniformMatrix4fv(uniform.iLocMVP, 1, GL_FALSE, temp);

	// pass normal transformation matrix
	Matrix4 NORM_TRANS = MV.invert().transpose();
	setGLMatrix(temp, NORM_TRANS); 
	glUniformMatrix4fv(iLocNormTrans, 1, GL_FALSE, temp);

	// pass project/viewing matrix to shader
	glUniformMatrix4fv(iLocP, 1, GL_FALSE, project_matrix.getTranspose());
	glUniformMatrix4fv(iLocV, 1, GL_FALSE, view_matrix.getTranspose());

	// pass light index
	glUniform1i(iLocLightIdx, light_idx);

	for (int i = 0; i < 3; ++i) {
		// direct, point, spot light
		glUniform4f(iLocLightInfo[i].ambient, lightInfo[i].ambient.x, lightInfo[i].ambient.y, lightInfo[i].ambient.z, lightInfo[i].ambient.w);
		glUniform4f(iLocLightInfo[i].diffuse, lightInfo[i].diffuse.x, lightInfo[i].diffuse.y, lightInfo[i].diffuse.z, lightInfo[i].diffuse.w);
		glUniform4f(iLocLightInfo[i].specular, lightInfo[i].specular.x, lightInfo[i].specular.y, lightInfo[i].specular.z, lightInfo[i].specular.w);
		glUniform4f(iLocLightInfo[i].position, lightInfo[i].position.x, lightInfo[i].position.y, lightInfo[i].position.z, lightInfo[i].position.w);
	}

	for (int i = 1; i <= 2; ++i) {
		// point, spot light
		glUniform1f(iLocLightInfo[i].constantAttenuation, lightInfo[i].constantAttenuation);
		glUniform1f(iLocLightInfo[i].linearAttenuation, lightInfo[i].linearAttenuation);
		glUniform1f(iLocLightInfo[i].quadraticAttenuation, lightInfo[i].quadraticAttenuation);
	}

	glUniform4f(iLocLightInfo[2].spotDirection, lightInfo[2].spotDirection.x, lightInfo[2].spotDirection.y, lightInfo[2].spotDirection.z, lightInfo[2].spotDirection.w);
	glUniform1f(iLocLightInfo[2].spotExponent, lightInfo[2].spotExponent);
	glUniform1f(iLocLightInfo[2].spotCutoff, lightInfo[2].spotCutoff);

}

// Render function for display rendering
void RenderScene(void) {
	// clear canvas
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	setUniforms();

	for (int i = 0; i < models[cur_idx].shapes.size(); i++) {
		// material properties
		glUniform4f(iLocKa, models[cur_idx].shapes[i].material.Ka.x, models[cur_idx].shapes[i].material.Ka.y, models[cur_idx].shapes[i].material.Ka.z, 1.0);
		glUniform4f(iLocKd, models[cur_idx].shapes[i].material.Kd.x, models[cur_idx].shapes[i].material.Kd.y, models[cur_idx].shapes[i].material.Kd.z, 1.0);
		glUniform4f(iLocKs, models[cur_idx].shapes[i].material.Ks.x, models[cur_idx].shapes[i].material.Ks.y, models[cur_idx].shapes[i].material.Ks.z, 1.0);
		glUniform1f(iLocShininess, shininess);

		// Vertex lighting at LHS
		glViewport(0, 0, (GLsizei)(WINDOW_WIDTH / 2), WINDOW_HEIGHT);
		glBindVertexArray(models[cur_idx].shapes[i].vao);
		glUniform1i(iLocVertex_or_perpixel, 0);
		glDrawArrays(GL_TRIANGLES, 0, models[cur_idx].shapes[i].vertex_count);

		// Pixel lighting at RHS
		glViewport((GLsizei)(WINDOW_WIDTH / 2), 0, (GLsizei)(WINDOW_WIDTH / 2), WINDOW_HEIGHT);
		glBindVertexArray(models[cur_idx].shapes[i].vao);
		glUniform1i(iLocVertex_or_perpixel, 1);
		glDrawArrays(GL_TRIANGLES, 0, models[cur_idx].shapes[i].vertex_count);
	}
}


void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	// [DO] Call back function for keyboard
	if (!(action == GLFW_PRESS)) return;

	uint64_t model_nums = (unsigned int)models.size();

	switch (key) {
	case GLFW_KEY_ESCAPE:
		exit(0);

	case GLFW_KEY_Z:
		cur_idx = (cur_idx + model_nums - 1) % model_nums;
		break;

	case GLFW_KEY_X:
		cur_idx = (cur_idx + 1) % model_nums;
		break;

	case GLFW_KEY_T:
		cur_trans_mode = GeoTranslation;
		break;

	case GLFW_KEY_S:
		cur_trans_mode = GeoScaling;
		break;

	case GLFW_KEY_R:
		cur_trans_mode = GeoRotation;
		break;

	case GLFW_KEY_L:
		light_idx = (light_idx + 1) % 3;
		cout << " Light Mode: " << ((light_idx == 0) ? "Directional" :(light_idx == 1) ? "Point" :"Spot") << " Light" << endl;
		break;

	case GLFW_KEY_K:
		cur_trans_mode = LightEdit;
		break;

	case GLFW_KEY_J:
		cur_trans_mode = ShininessEdit;
		break;
	}
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	// [DO] scroll up positive, otherwise it would be negtive
	const float translation_factor = 0.01;
	const float scaling_factor = 0.01;
	const float rotation_factor = 0.2;
	const float shininess_changing_factor = 2.0;
	const float cutoff_changing_factor = 0.5;
	const float diffuse_changing_factor = 0.1;

	switch (cur_trans_mode)
	{
		case GeoTranslation:
			models[cur_idx].position.z += yoffset * translation_factor;
			break;

		case GeoScaling:
			models[cur_idx].scale.z += yoffset * scaling_factor;
			break;

		case GeoRotation:
			models[cur_idx].rotation.z += PI / 180.0 * yoffset * rotation_factor;
			break;

		case ShininessEdit:
			shininess = max(shininess + yoffset * shininess_changing_factor, 1);
			break;

		case LightEdit:
			if (light_idx == 2) { // spotlight mode 
				lightInfo[2].spotCutoff += yoffset * cutoff_changing_factor * PI / 180.0;
			}else if (light_idx == 1) {
				lightInfo[1].diffuse += Vector4(yoffset * diffuse_changing_factor, yoffset * diffuse_changing_factor, yoffset * diffuse_changing_factor, yoffset * diffuse_changing_factor);
			}else if (light_idx == 0) {
				lightInfo[0].diffuse += Vector4(yoffset * diffuse_changing_factor, yoffset * diffuse_changing_factor, yoffset * diffuse_changing_factor, yoffset * diffuse_changing_factor);
			}
			break;
	}
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	// [DO] mouse press callback function
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		if (action == GLFW_PRESS) {
			mouse_pressed = true;
		}
		if (action == GLFW_RELEASE) {
			mouse_pressed = false;
			starting_press_x = -1;
			starting_press_y = -1;
		}
	}
}

static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos)
{
	// [DO] cursor position callback function
	if (!mouse_pressed) return;

	if (starting_press_x == -1 || starting_press_y == -1) {
		starting_press_x = xpos;
		starting_press_y = ypos;
		return;
	}

	int delta_x = xpos - starting_press_x;
	int delta_y = -(ypos - starting_press_y); // move up: - -> +

	const float translation_factor = 0.01;
	const float scaling_factor = 0.01;
	const float rotation_factor = 0.2;
	const float light_translation_factor = 0.01;

	switch (cur_trans_mode) 
	{
		case GeoTranslation:
			models[cur_idx].position.x += delta_x * translation_factor;
			models[cur_idx].position.y += delta_y * translation_factor;
			break;

		case GeoScaling:
			models[cur_idx].scale.x -= delta_x * scaling_factor;
			models[cur_idx].scale.y += delta_y * scaling_factor;
			break;

		case GeoRotation:
			models[cur_idx].rotation.x += PI / 180.0 * delta_y * rotation_factor;
			models[cur_idx].rotation.y -= PI / 180.0 * delta_x * rotation_factor;
			break;

		case LightEdit:
			lightInfo[light_idx].position.x += delta_x * light_translation_factor;
			lightInfo[light_idx].position.y += delta_y * light_translation_factor;
			break;
	}

	starting_press_x = xpos;
	starting_press_y = ypos;
}

void setUniformVariables(GLint program) {

	iLocVertex_or_perpixel = glGetUniformLocation(program, "vertex_or_perpixel");

	iLocP = glGetUniformLocation(program, "project_matrix");
	iLocV = glGetUniformLocation(program, "view_matrix");
	iLocModelMatrix = glGetUniformLocation(program, "model_matrix");
	iLocMV = glGetUniformLocation(program, "mv");
	uniform.iLocMVP = glGetUniformLocation(program, "mvp");
	iLocNormTrans = glGetUniformLocation(program, "normTrans");

	iLocLightIdx = glGetUniformLocation(program, "lightIdx");

	iLocKa = glGetUniformLocation(program, "material.Ka");
	iLocKd = glGetUniformLocation(program, "material.Kd");
	iLocKs = glGetUniformLocation(program, "material.Ks");
	iLocShininess = glGetUniformLocation(program, "material.shininess");

	/*
	for (int i = 0; i < 3; ++i) {
		auto j = to_string(i);
		//string data = "light[" + j + "].position";
		//const GLchar* c_str = data.c_str();
		const GLchar* pos = ("light[" + j + "].position").c_str();
		iLocLightInfo[i].position = glGetUniformLocation(program, pos);

		const GLchar* La = ("light[" + j + "].La").c_str();
		iLocLightInfo[i].ambient = glGetUniformLocation(program, La);

		// [TODO] debug
		const GLchar* Ld = ("light[" + j + "].Ld").c_str();
		iLocLightInfo[i].diffuse = glGetUniformLocation(program, Ld);

		const GLchar* Ls = ("light[" + j + "].Ls").c_str();
		iLocLightInfo[i].specular = glGetUniformLocation(program, Ls);

		const GLchar* spot_D = ("light[" + j + "].spotDirection").c_str();
		iLocLightInfo[i].spotDirection = glGetUniformLocation(program, spot_D);

		const GLchar* spot_C = ("light[" + j + "].spotCutoff").c_str();
		iLocLightInfo[i].spotCutoff = glGetUniformLocation(program, spot_C);

		const GLchar* spot_E = ("light[" + j + "].spotExponent").c_str();
		iLocLightInfo[i].spotExponent = glGetUniformLocation(program, spot_E);

		const GLchar*  CA = ("light[" + j + "].spotExponent").c_str();
		iLocLightInfo[i].constantAttenuation = glGetUniformLocation(program, CA);

		const GLchar*  LA = ("light[" + j + "].linearAttenuation").c_str();
		iLocLightInfo[i].linearAttenuation = glGetUniformLocation(program, LA);

		const GLchar*  QA = ("light[" + j + "].quadraticAttenuation").c_str();
		iLocLightInfo[i].quadraticAttenuation = glGetUniformLocation(program, QA);
	}
	*/

	iLocLightInfo[0].position = glGetUniformLocation(program, "light[0].position");
	iLocLightInfo[0].ambient = glGetUniformLocation(program, "light[0].La");
	iLocLightInfo[0].diffuse = glGetUniformLocation(program, "light[0].Ld");
	iLocLightInfo[0].specular = glGetUniformLocation(program, "light[0].Ls");
	iLocLightInfo[0].spotDirection = glGetUniformLocation(program, "light[0].spotDirection");
	iLocLightInfo[0].spotCutoff = glGetUniformLocation(program, "light[0].spotCutoff");
	iLocLightInfo[0].spotExponent = glGetUniformLocation(program, "light[0].spotExponent");
	iLocLightInfo[0].constantAttenuation = glGetUniformLocation(program, "light[0].constantAttenuation");
	iLocLightInfo[0].linearAttenuation = glGetUniformLocation(program, "light[0].linearAttenuation");
	iLocLightInfo[0].quadraticAttenuation = glGetUniformLocation(program, "light[0].quadraticAttenuation");

	iLocLightInfo[1].position = glGetUniformLocation(program, "light[1].position");
	iLocLightInfo[1].ambient = glGetUniformLocation(program, "light[1].La");
	iLocLightInfo[1].diffuse = glGetUniformLocation(program, "light[1].Ld");
	iLocLightInfo[1].specular = glGetUniformLocation(program, "light[1].Ls");
	iLocLightInfo[1].spotDirection = glGetUniformLocation(program, "light[1].spotDirection");
	iLocLightInfo[1].spotCutoff = glGetUniformLocation(program, "light[1].spotCutoff");
	iLocLightInfo[1].spotExponent = glGetUniformLocation(program, "light[1].spotExponent");
	iLocLightInfo[1].constantAttenuation = glGetUniformLocation(program, "light[1].constantAttenuation");
	iLocLightInfo[1].linearAttenuation = glGetUniformLocation(program, "light[1].linearAttenuation");
	iLocLightInfo[1].quadraticAttenuation = glGetUniformLocation(program, "light[1].quadraticAttenuation");

	iLocLightInfo[2].position = glGetUniformLocation(program, "light[2].position");
	iLocLightInfo[2].ambient = glGetUniformLocation(program, "light[2].La");
	iLocLightInfo[2].diffuse = glGetUniformLocation(program, "light[2].Ld");
	iLocLightInfo[2].specular = glGetUniformLocation(program, "light[2].Ls");
	iLocLightInfo[2].spotDirection = glGetUniformLocation(program, "light[2].spotDirection");
	iLocLightInfo[2].spotCutoff = glGetUniformLocation(program, "light[2].spotCutoff");
	iLocLightInfo[2].spotExponent = glGetUniformLocation(program, "light[2].spotExponent");
	iLocLightInfo[2].constantAttenuation = glGetUniformLocation(program, "light[2].constantAttenuation");
	iLocLightInfo[2].linearAttenuation = glGetUniformLocation(program, "light[2].linearAttenuation");
	iLocLightInfo[2].quadraticAttenuation = glGetUniformLocation(program, "light[2].quadraticAttenuation");

	// [TODO] debug
	// iLocLightInfo[1].diffuse = glGetUniformLocation(program, "light[1].Ld");
}

void setShaders()
{
	GLuint v, f, p;
	char *vs = NULL;
	char *fs = NULL;

	v = glCreateShader(GL_VERTEX_SHADER);
	f = glCreateShader(GL_FRAGMENT_SHADER);

	vs = textFileRead("shader.vs");
	fs = textFileRead("shader.fs");

	glShaderSource(v, 1, (const GLchar**)&vs, NULL);
	glShaderSource(f, 1, (const GLchar**)&fs, NULL);

	free(vs);
	free(fs);

	GLint success;
	char infoLog[1000];
	// compile vertex shader
	glCompileShader(v);
	// check for shader compile errors
	glGetShaderiv(v, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(v, 1000, NULL, infoLog);
		std::cout << "ERROR: VERTEX SHADER COMPILATION FAILED\n" << infoLog << std::endl;
	}

	// compile fragment shader
	glCompileShader(f);
	// check for shader compile errors
	glGetShaderiv(f, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(f, 1000, NULL, infoLog);
		std::cout << "ERROR: FRAGMENT SHADER COMPILATION FAILED\n" << infoLog << std::endl;
	}

	// create program object
	p = glCreateProgram();

	// attach shaders to program object
	glAttachShader(p, f);
	glAttachShader(p, v);

	// link program
	glLinkProgram(p);
	// check for linking errors
	glGetProgramiv(p, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(p, 1000, NULL, infoLog);
		std::cout << "ERROR: SHADER PROGRAM LINKING FAILED\n" << infoLog << std::endl;
	}

	glDeleteShader(v);
	glDeleteShader(f);

	if (success)
		glUseProgram(p);
	else
	{
		system("pause");
		exit(123);
	}

	setUniformVariables(p);
}

void normalization(tinyobj::attrib_t* attrib, vector<GLfloat>& vertices, vector<GLfloat>& colors, vector<GLfloat>& normals, tinyobj::shape_t* shape)
{
	vector<float> xVector, yVector, zVector;
	float minX = 10000, maxX = -10000, minY = 10000, maxY = -10000, minZ = 10000, maxZ = -10000;

	// find out min and max value of X, Y and Z axis
	for (int i = 0; i < attrib->vertices.size(); i++)
	{
		//maxs = max(maxs, attrib->vertices.at(i));
		if (i % 3 == 0)
		{

			xVector.push_back(attrib->vertices.at(i));

			if (attrib->vertices.at(i) < minX)
			{
				minX = attrib->vertices.at(i);
			}

			if (attrib->vertices.at(i) > maxX)
			{
				maxX = attrib->vertices.at(i);
			}
		}
		else if (i % 3 == 1)
		{
			yVector.push_back(attrib->vertices.at(i));

			if (attrib->vertices.at(i) < minY)
			{
				minY = attrib->vertices.at(i);
			}

			if (attrib->vertices.at(i) > maxY)
			{
				maxY = attrib->vertices.at(i);
			}
		}
		else if (i % 3 == 2)
		{
			zVector.push_back(attrib->vertices.at(i));

			if (attrib->vertices.at(i) < minZ)
			{
				minZ = attrib->vertices.at(i);
			}

			if (attrib->vertices.at(i) > maxZ)
			{
				maxZ = attrib->vertices.at(i);
			}
		}
	}

	float offsetX = (maxX + minX) / 2;
	float offsetY = (maxY + minY) / 2;
	float offsetZ = (maxZ + minZ) / 2;

	for (int i = 0; i < attrib->vertices.size(); i++)
	{
		if (offsetX != 0 && i % 3 == 0)
		{
			attrib->vertices.at(i) = attrib->vertices.at(i) - offsetX;
		}
		else if (offsetY != 0 && i % 3 == 1)
		{
			attrib->vertices.at(i) = attrib->vertices.at(i) - offsetY;
		}
		else if (offsetZ != 0 && i % 3 == 2)
		{
			attrib->vertices.at(i) = attrib->vertices.at(i) - offsetZ;
		}
	}

	float greatestAxis = maxX - minX;
	float distanceOfYAxis = maxY - minY;
	float distanceOfZAxis = maxZ - minZ;

	if (distanceOfYAxis > greatestAxis)
	{
		greatestAxis = distanceOfYAxis;
	}

	if (distanceOfZAxis > greatestAxis)
	{
		greatestAxis = distanceOfZAxis;
	}

	float scale = greatestAxis / 2;

	for (int i = 0; i < attrib->vertices.size(); i++)
	{
		//std::cout << i << " = " << (double)(attrib.vertices.at(i) / greatestAxis) << std::endl;
		attrib->vertices.at(i) = attrib->vertices.at(i) / scale;
	}
	size_t index_offset = 0;
	for (size_t f = 0; f < shape->mesh.num_face_vertices.size(); f++) {
		int fv = shape->mesh.num_face_vertices[f];

		// Loop over vertices in the face.
		for (size_t v = 0; v < fv; v++) {
			// access to vertex
			tinyobj::index_t idx = shape->mesh.indices[index_offset + v];
			vertices.push_back(attrib->vertices[3 * idx.vertex_index + 0]);
			vertices.push_back(attrib->vertices[3 * idx.vertex_index + 1]);
			vertices.push_back(attrib->vertices[3 * idx.vertex_index + 2]);
			// Optional: vertex colors
			colors.push_back(attrib->colors[3 * idx.vertex_index + 0]);
			colors.push_back(attrib->colors[3 * idx.vertex_index + 1]);
			colors.push_back(attrib->colors[3 * idx.vertex_index + 2]);
			// Optional: vertex normals
			if (idx.normal_index >= 0) {
				normals.push_back(attrib->normals[3 * idx.normal_index + 0]);
				normals.push_back(attrib->normals[3 * idx.normal_index + 1]);
				normals.push_back(attrib->normals[3 * idx.normal_index + 2]);
			}
		}
		index_offset += fv;
	}
}

string GetBaseDir(const string& filepath) {
	if (filepath.find_last_of("/\\") != std::string::npos)
		return filepath.substr(0, filepath.find_last_of("/\\"));
	return "";
}

void LoadModels(string model_path)
{
	vector<tinyobj::shape_t> shapes;
	vector<tinyobj::material_t> materials;
	tinyobj::attrib_t attrib;
	vector<GLfloat> vertices;
	vector<GLfloat> colors;
	vector<GLfloat> normals;

	string err;
	string warn;

	string base_dir = GetBaseDir(model_path); // handle .mtl with relative path

#ifdef _WIN32
	base_dir += "\\";
#else
	base_dir += "/";
#endif

	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, model_path.c_str(), base_dir.c_str());

	if (!warn.empty()) {
		cout << warn << std::endl;
	}

	if (!err.empty()) {
		cerr << err << std::endl;
	}

	if (!ret) {
		exit(1);
	}

	printf("Load Models Success ! Shapes size %d Material size %d\n", int(shapes.size()), int(materials.size()));
	model tmp_model;

	vector<PhongMaterial> allMaterial;
	for (int i = 0; i < materials.size(); i++)
	{
		PhongMaterial material;
		material.Ka = Vector3(materials[i].ambient[0], materials[i].ambient[1], materials[i].ambient[2]);
		material.Kd = Vector3(materials[i].diffuse[0], materials[i].diffuse[1], materials[i].diffuse[2]);
		material.Ks = Vector3(materials[i].specular[0], materials[i].specular[1], materials[i].specular[2]);
		allMaterial.push_back(material);
	}

	for (int i = 0; i < shapes.size(); i++)
	{

		vertices.clear();
		colors.clear();
		normals.clear();
		normalization(&attrib, vertices, colors, normals, &shapes[i]);
		// printf("Vertices size: %d", vertices.size() / 3);

		Shape tmp_shape;
		glGenVertexArrays(1, &tmp_shape.vao);
		glBindVertexArray(tmp_shape.vao);

		glGenBuffers(1, &tmp_shape.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, tmp_shape.vbo);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GL_FLOAT), &vertices.at(0), GL_STATIC_DRAW);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
		tmp_shape.vertex_count = vertices.size() / 3;

		glGenBuffers(1, &tmp_shape.p_color);
		glBindBuffer(GL_ARRAY_BUFFER, tmp_shape.p_color);
		glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(GL_FLOAT), &colors.at(0), GL_STATIC_DRAW);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);

		glGenBuffers(1, &tmp_shape.p_normal);
		glBindBuffer(GL_ARRAY_BUFFER, tmp_shape.p_normal);
		glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(GL_FLOAT), &normals.at(0), GL_STATIC_DRAW);
		glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, 0);

		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);

		// not support per face material, use material of first face
		if (allMaterial.size() > 0)
			tmp_shape.material = allMaterial[shapes[i].mesh.material_ids[0]];
		tmp_model.shapes.push_back(tmp_shape);
	}
	shapes.clear();
	materials.clear();
	models.push_back(tmp_model);
}

void initParameter()
{
	// [DO] Setup some parameters if you need
	proj.left = -1;
	proj.right = 1;
	proj.top = 1;
	proj.bottom = -1;
	proj.nearClip = 0.001;
	proj.farClip = 100.0;
	proj.fovy = 80;
	proj.aspect = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;

	main_camera.position = Vector3(0.0f, 0.0f, 2.0f);
	main_camera.center = Vector3(0.0f, 0.0f, 0.0f);
	main_camera.up_vector = Vector3(0.0f, 1.0f, 0.0f);

	setViewingMatrix();
	setPerspective();	//set default projection matrix as perspective matrix

	// properties for lighting
	shininess = 64;

	lightInfo[0].position = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	lightInfo[0].ambient = Vector4(0.15f, 0.15f, 0.15f, 1.0f);
	lightInfo[0].diffuse = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	lightInfo[0].specular = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	lightInfo[1].position = Vector4(0.0f, 2.0f, 1.0f, 1.0f);
	lightInfo[1].ambient = Vector4(0.15f, 0.15f, 0.15f, 1.0f);
	lightInfo[1].diffuse = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	lightInfo[1].specular = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	lightInfo[1].constantAttenuation = 0.01;
	lightInfo[1].linearAttenuation = 0.8;
	lightInfo[1].quadraticAttenuation = 0.1f;

	lightInfo[2].position = Vector4(0.0f, 0.0f, 2.0f, 1.0f);
	lightInfo[2].ambient = Vector4(0.15f, 0.15f, 0.15f, 1.0f);
	lightInfo[2].diffuse = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	lightInfo[2].specular = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	lightInfo[2].spotDirection = Vector4(0.0f, 0.0f, -1.0f, 0.0f);
	lightInfo[2].spotExponent = 50;
	lightInfo[2].spotCutoff = 30 * PI / 180.0;
	lightInfo[2].constantAttenuation = 0.5;
	lightInfo[2].linearAttenuation = 0.3;
	lightInfo[2].quadraticAttenuation = 0.6f;
}

void setupRC()
{
	// setup shaders
	setShaders();
	initParameter();

	// OpenGL States and Values
	glClearColor(0.2, 0.2, 0.2, 1.0);
	vector<string> model_list{ "../NormalModels/bunny5KN.obj", "../NormalModels/dragon10KN.obj", "../NormalModels/lucy25KN.obj", "../NormalModels/teapot4KN.obj", "../NormalModels/dolphinN.obj" };
	// [DO] Load five model at here
	for (int i = 0; i <= 4; i++)
	{
		LoadModels(model_list[i]);
	}
}

void glPrintContextInfo(bool printExtension)
{
	cout << "GL_VENDOR = " << (const char*)glGetString(GL_VENDOR) << endl;
	cout << "GL_RENDERER = " << (const char*)glGetString(GL_RENDERER) << endl;
	cout << "GL_VERSION = " << (const char*)glGetString(GL_VERSION) << endl;
	cout << "GL_SHADING_LANGUAGE_VERSION = " << (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION) << endl;
	if (printExtension)
	{
		GLint numExt;
		glGetIntegerv(GL_NUM_EXTENSIONS, &numExt);
		cout << "GL_EXTENSIONS =" << endl;
		for (GLint i = 0; i < numExt; i++)
		{
			cout << "\t" << (const char*)glGetStringi(GL_EXTENSIONS, i) << endl;
		}
	}
}


int main(int argc, char **argv)
{
	// initial glfw
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // fix compilation on OS X
#endif


	// create window
	GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "110062619 HW2", NULL, NULL);
	if (window == NULL)
	{
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	// load OpenGL function pointer
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		return -1;
	}

	// register glfw callback functions
	glfwSetKeyCallback(window, KeyCallback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetCursorPosCallback(window, cursor_pos_callback);

	glfwSetFramebufferSizeCallback(window, ChangeSize);
	glEnable(GL_DEPTH_TEST);
	// Setup render context
	setupRC();

	// main loop
	while (!glfwWindowShouldClose(window))
	{
		// render
		RenderScene();

		// swap buffer from back to front
		glfwSwapBuffers(window);

		// Poll input event
		glfwPollEvents();
	}

	// just for compatibiliy purposes
	return 0;
}