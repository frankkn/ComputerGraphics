#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec3 aNormal;

out vec4 vertex_color;
out vec3 vertex_normal;
out vec3 vertex_view;
out vec3 vertex_position; // *

//uniform mat4 mvp;

uniform mat4 project_matrix;	// projection matrix
uniform mat4 view_matrix;	// camera viewing transformation matrix
uniform mat4 model_matrix ; // model_matrix = T * R * S
uniform mat4 mv;
uniform mat4 mvp;
uniform mat4 normTrans; // MV.invert.tranpose


struct LightInfo{
	vec4 position;
	vec4 La; // ambientIntensity		
	vec4 Ld; // diffuseIntensity
	vec4 Ls; // specularIntensity
	
	vec4 spotDirection;
	float spotExponent;
	float spotCutoff;

	// lighting properties (spot light)
	float constantAttenuation;
	float linearAttenuation;
	float quadraticAttenuation;
};

struct MaterialInfo{
	vec4 Ka;
	vec4 Kd;
	vec4 Ks;
	float shininess;
};

uniform int lightIdx;			// pixel lighting
uniform int lightIdxv;			// vertex lighting
uniform LightInfo light[3];
uniform MaterialInfo material;

vec4 directionalLight(){
	// calculate light_position, viewing_position, vertex_position
    vertex_position = (mv * vec4(aPos, 1.0)).xyz;
    vec3 light_pos = (view_matrix * light[0].position).xyz;
    vec3 view_pos = vec3(0, 0, 0); // because we are in viewing space
    
    // calculate light_vector, viewing_vector, halfway_vector
    vec3 light_vector = normalize( light_pos );
    vec3 view_vector = normalize( view_pos - vertex_position );
    vec3 halfway_vector = normalize( light_vector + view_vector );
    
    // calculate ambient
    vec3 ambient;
	ambient[0] = (light[1].La[0] * material.Ka[0]); 
    ambient[1] = (light[1].La[1] * material.Ka[1]);
	ambient[2] = (light[1].La[2] * material.Ka[2]);

    // calculate diffuse
    float diffuse_rate = max( dot(light_vector, vertex_normal), 0 );
    vec3 diffuse = (diffuse_rate * light[1].Ld * material.Kd).xyz;

    // calculate specular
    float specular_rate = pow( max( dot(halfway_vector, vertex_normal), 0 ), material.shininess );
    vec3 specular = (specular_rate * light[1].Ls * material.Ks).xyz;
 
	return vec4( (ambient + diffuse + specular) , 1.0);
}

vec4 pointLight() {
	// calculate light_position, viewing_position, vertex_position
    vertex_position = (mv * vec4(aPos, 1.0)).xyz;
    vec3 light_pos = (view_matrix * light[1].position).xyz;
    vec3 view_pos = vec3(0, 0, 0); // because we are in viewing space
    
    // calculate light_vector, viewing_vector, halfway_vector
    vec3 light_vector = normalize( light_pos - vertex_position );
    vec3 view_vector = normalize( view_pos - vertex_position );
    vec3 halfway_vector = normalize( light_vector + view_vector );
    
    // calculate ambient
    vec3 ambient = (light[1].La * material.Ka).xyz;
	/*
	ambient[0] = (light[1].La[0] * material.Ka[0]); 
    ambient[1] = (light[1].La[1] * material.Ka[1]);
	ambient[2] = (light[1].La[2] * material.Ka[2]);
	*/

    // calculate diffuse
    float diffuse_rate = max( dot(light_vector, vertex_normal), 0 );
    vec3 diffuse = (diffuse_rate * light[1].Ld * material.Kd).xyz;

    // calculate specular
    float specular_rate = pow( max( dot(halfway_vector, vertex_normal), 0 ), material.shininess );
    vec3 specular = (specular_rate * light[1].Ls * material.Ks).xyz;
    
    // attenuation
    float dis = length(light_pos - vertex_position); // distance
    float attenuation =  1 / (light[1].constantAttenuation + light[1].linearAttenuation * dis + light[1].quadraticAttenuation * dis * dis);
    
	return vec4( (ambient + attenuation * (diffuse + specular)) , 1.0) ;

}


vec4 spotLight(){
	// calculate light_position, viewing_position, vertex_position
    vertex_position = (mv * vec4(aPos, 1.0)).xyz;
    vec3 light_pos = (view_matrix * light[2].position).xyz;
    vec3 view_pos = vec3(0, 0, 0); // because we are in viewing space
    
    // calculate light_vector, viewing_vector, halfway_vector
    vec3 light_vector = normalize( light_pos - vertex_position );
    vec3 view_vector = normalize( view_pos - vertex_position );
    vec3 halfway_vector = normalize( light_vector + view_vector );
    
    // calculate ambient
    vec3 ambient;
	ambient[0] = (light[2].La[0] * material.Ka[0]); 
    ambient[1] = (light[2].La[1] * material.Ka[1]);
	ambient[2] = (light[2].La[2] * material.Ka[2]);

    // calculate diffuse
    float diffuse_rate = max( dot(light_vector, vertex_normal), 0 );
    vec3 diffuse = (diffuse_rate * light[1].Ld * material.Kd).xyz;

    // calculate specular
    float specular_rate = pow( max( dot(halfway_vector, vertex_normal), 0 ), material.shininess );
    vec3 specular = (specular_rate * light[2].Ls * material.Ks).xyz;
    
    // attenuation
    float dis = length(light_pos - vertex_position); // distance
    float attenuation =  1 / (light[2].constantAttenuation + light[2].linearAttenuation * dis + light[2].quadraticAttenuation * dis * dis);
    
    
    // calculate spotlight effect
    float cos_vertex_direction = dot(-light_vector, light[2].spotDirection.xyz); // cosine of angle between vector from light_pos to vertex_pos and direction
	float spotlight_effect = (cos_vertex_direction < cos(light[2].spotCutoff)) ? 0: pow( max(cos_vertex_direction, 0), light[2].spotExponent );
    
    return vec4( (ambient + attenuation * spotlight_effect * (diffuse + specular)) , 1.0) ;
}


void main()
{

	//vec4 vertexInView = view_matrix  * vec4(aPos, 1.0); // [TODO] * M ?
	//vec3 tmp = (transpose(inverse(view_matrix * model_matrix )) * vec4(aNormal,0.0)).xyz;
	//vertex_view = vertexInView.xyz;
	//vertex_normal = tmp;
	vertex_normal = normalize( (normTrans * vec4(aNormal, 1.0)).xyz );

	// vec3 N = normalize(tmp);
	// vec3 N =  normalize( (normTrans * vec4(aNormal, 1.0)).xyz );
	// vec3 V = -vertexInView.xyz; // V = vertex position in viewing space	

	if(lightIdx == 0)
	{
		vertex_color = directionalLight();
	}
	else if(lightIdx == 1)
	{
		vertex_color = pointLight();
	}
	else if(lightIdx == 2)
	{
		vertex_color = spotLight();
	}
	
	gl_Position = project_matrix * view_matrix * model_matrix * vec4(aPos, 1.0);
		
}

