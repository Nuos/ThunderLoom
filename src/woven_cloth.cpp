#include "woven_cloth.h"
#ifndef WC_NO_FILES
#define REALWORLD_UV_WIF_TO_MM 10.0
#include "wif/wif.cpp"
#include "wif/ini.cpp"
#endif

// For M_PI etc.
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>

// -- 3D Vector data structure -- //
typedef struct
{
    float x,y,z,w;
} wcVector;

static wcVector wcvector(float x, float y, float z)
{
    wcVector ret = {x,y,z,0.f};
    return ret;
}

static float wcVector_magnitude(wcVector v)
{
    return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

static wcVector wcVector_normalize(wcVector v)
{
    wcVector ret;
    float inv_mag = 1.f/sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    ret.x = v.x * inv_mag;
    ret.y = v.y * inv_mag;
    ret.z = v.z * inv_mag;
    return ret;
}

static float wcVector_dot(wcVector a, wcVector b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static wcVector wcVector_cross(wcVector a, wcVector b)
{
    wcVector ret;
    ret.x = a.y*b.z - a.z*b.y;
    ret.y = a.z*b.x - a.x*b.z;
    ret.z = a.x*b.y - a.y*b.x;
    return ret;
}

static wcVector wcVector_add(wcVector a, wcVector b)
{
    wcVector ret;
    ret.x = a.x + b.x;
    ret.y = a.y + b.y;
    ret.z = a.z + b.z;
    return ret;
}

// -- //

//atof equivalent function wich is not dependent
//on local. Heavily inspired by inplementation in K & R.
static double str2d(char s[]) {
    double val, power;
    int i, sign;

    for (i = 0; isspace(s[i]); i++)
        ;
    sign = (s[i] == '-') ? -1 : 1;
    if (s[i] == '+' || s[i] == '-')
        i++;
    for (val = 0.0; isdigit(s[i]); i++)
        val = 10.0 * val + (s[i] - '0');
    if (s[i] == '.')
        i++;
    for (power = 1.0; isdigit(s[i]); i++) {
        val = 10.0 * val + (s[i] - '0');
        power *= 10.0;
    }
    return sign * val / power;
}

static float wcClamp(float x, float min, float max)
{
    return (x < min) ? min : (x > max) ? max : x;
}

/* Tiny Encryption Algorithm by David Wheeler and Roger Needham */
/* Taken from mitsuba source code. */
static uint64_t sampleTEA(uint32_t v0, uint32_t v1, int rounds)
{
	uint32_t sum = 0;

	for (int i=0; i<rounds; ++i) {
		sum += 0x9e3779b9;
		v0 += ((v1 << 4) + 0xA341316C) ^ (v1 + sum) ^ ((v1 >> 5) + 0xC8013EA4);
		v1 += ((v0 << 4) + 0xAD90777D) ^ (v0 + sum) ^ ((v0 >> 5) + 0x7E95761E);
	}

	return ((uint64_t) v1 << 32) + v0;
}

//From Mitsuba
static float sampleTEASingle(uint32_t v0, uint32_t v1, int rounds)
{
    /* Trick from MTGP: generate an uniformly distributed
    single precision number in [1,2) and subtract 1. */
    union {
        uint32_t u;
        float f;
    } x;
    x.u = ((sampleTEA(v0, v1, rounds) & 0xFFFFFFFF) >> 9) | 0x3f800000UL;
    return x.f - 1.0f;
}

void sample_cosine_hemisphere(float sample_x, float sample_y, float *p_x,
        float *p_y, float *p_z)
{
    //sample uniform disk concentric
    // From mitsuba warp.cpp
	float r1 = 2.0f*sample_x - 1.0f;
	float r2 = 2.0f*sample_y - 1.0f;

// Modified concencric map code with less branching (by Dave Cline), see
// http://psgraphics.blogspot.ch/2011/01/improved-code-for-concentric-map.html
	float phi, r;
	if (r1 == 0 && r2 == 0) {
		r = phi = 0;
	} else if (r1*r1 > r2*r2) {
		r = r1;
		phi = (M_PI/4.0f) * (r2/r1);
	} else {
		r = r2;
		phi = (M_PI_2) - (r1/r2) * (M_PI/4.0f);
	}

    *p_x = r * cosf(phi);
    *p_y = r * sinf(phi);
    *p_z = sqrtf(1.0f - (*p_x)*(*p_x) - (*p_y)*(*p_y));
}

void sample_uniform_hemisphere(float sample_x, float sample_y, float *p_x,
        float *p_y, float *p_z)
{
    //Source: http://mathworld.wolfram.com/SpherePointPicking.html
    float theta = M_PI*2.f*sample_x;
    float phi   = acos(2.f*sample_y - 1.f);
    *p_x = cosf(theta)*cosf(phi);
    *p_y = sinf(theta)*cosf(phi);
    *p_z = sinf(phi);
}

void calculate_segment_uv_and_normal(wcPatternData *pattern_data,
        const wcWeaveParameters *params,wcIntersectionData *intersection_data)
{
    //Calculate the yarn-segment-local u v coordinates along the curved cylinder
    //NOTE: This is different from how Irawan does it
    /*segment_u = asinf(x*sinf(params->umax));
        segment_v = asinf(y);*/
    //TODO(Vidar): Use a parameter for choosing model?
    float segment_u = pattern_data->y*wc_yarn_type_get_umax(params,
        pattern_data->yarn_type,
		intersection_data->context);
    float segment_v = pattern_data->x*M_PI_2;

    //Calculate the normal in yarn-local coordinates
    float normal_x = sinf(segment_v);
    float normal_y = sinf(segment_u)*cosf(segment_v);
    float normal_z = cosf(segment_u)*cosf(segment_v);
    
    //TODO(Vidar): This is pretty weird, right? Now x and y are not yarn-local
    // anymore...
    // Transform the normal back to shading space
    if(!pattern_data->warp_above){
        float tmp = normal_x;
        normal_x  = normal_y;
        normal_y  = -tmp;
    }

    pattern_data->u        = segment_u;
    pattern_data->v        = segment_v;
    pattern_data->normal_x = normal_x;
    pattern_data->normal_y = normal_y;
    pattern_data->normal_z = normal_z;
}


static const int halton_4_base[] = {2, 3, 5, 7};
static const float halton_4_base_inv[] =
        {1.f/2.f, 1.f/3.f, 1.f/5.f, 1.f/7.f};

//Sets the elements of val to the n-th 4-dimensional point
//in the Halton sequence
static void halton_4(int n, float val[]){
    int j;
    for(j=0;j<4;j++){
        float f = 1.f;
        int i = n;
        val[j] = 0.f;
        while(i>0){
            f = f * halton_4_base_inv[j];
            val[j] = val[j] + f * (i % halton_4_base[j]);
            i = i / halton_4_base[j];
        }
    }
}

void wcFinalizeWeaveParameters(wcWeaveParameters *params)
{
    //Calculate normalization factor for the specular reflection
	if (params->pattern) {
		size_t nLocationSamples = 100;
		size_t nDirectionSamples = 1000;
		params->specular_normalization = 1.f;

		float highest_result = 0.f;

		// Temporarily disable intensity variation...
		float tmp_intensity_fineness = params->intensity_fineness;
		params->intensity_fineness = 0.f;

		// Normalize by the largest reflection across all uv coords and
		// incident directions
		for( uint32_t yarn_type = 0; yarn_type < params->num_yarn_types;
			yarn_type++){
			for (size_t i = 0; i < nLocationSamples; i++) {
				float result = 0.0f;
				float halton_point[4];
				halton_4(i + 50, halton_point);
				wcPatternData pattern_data;
				// Pick a random location on a segment rectangle...
				pattern_data.x = -1.f + 2.f*halton_point[0];
				pattern_data.y = -1.f + 2.f*halton_point[1];
				pattern_data.length = 1.f;
				pattern_data.width = 1.f;
				pattern_data.warp_above = 0;
				pattern_data.yarn_type = yarn_type;
				wcIntersectionData intersection_data;
				intersection_data.context=0;
				calculate_segment_uv_and_normal(&pattern_data, params,
					&intersection_data);
				pattern_data.total_index_x = 0;
				pattern_data.total_index_y = 0;

				sample_uniform_hemisphere(halton_point[2], halton_point[3],
					&intersection_data.wi_x, &intersection_data.wi_y,
					&intersection_data.wi_z);

				for (size_t j = 0; j < nDirectionSamples; j++) {
					float halton_direction[4];
					halton_4(j + 50 + nLocationSamples, halton_direction);
					// Since we use cosine sampling here, we can ignore the cos term
					// in the integral
					sample_cosine_hemisphere(halton_direction[0], halton_direction[1],
						&intersection_data.wo_x, &intersection_data.wo_y,
						&intersection_data.wo_z);
					result += wcEvalSpecular(intersection_data, pattern_data, params);
				}
				if (result > highest_result) {
					highest_result = result;
				}
			}
		}

		if (highest_result <= 0.0001f) {
			params->specular_normalization = 0.f;
		}
		else {
			params->specular_normalization =
				(float)nDirectionSamples / highest_result;
		}
		params->intensity_fineness = tmp_intensity_fineness;
	}
}

#ifndef WC_NO_FILES

//NOTE(Vidar): In case we add support for more file formats
void wcWeavePatternFromFile(wcWeaveParameters *params, const char *filename)
{
    wcWeavePatternFromWIF(params,filename);
}
#ifdef WC_WCHAR
void wcWeavePatternFromFile_wchar(wcWeaveParameters *params,
    const wchar_t *filename)
{
    wcWeavePatternFromWIF_wchar(params,filename);
}
#endif

void wcWeavePatternFromWIF(wcWeaveParameters *params, const char *filename)
{
    WeaveData *data = wif_read(filename);
    wif_get_pattern(params, data,
        &params->pattern_width, &params->pattern_height,
        &params->pattern_realwidth, &params->pattern_realheight);
    wif_free_weavedata(data);
    wcFinalizeWeaveParameters(params);
}

#ifdef WC_WCHAR
void wcWeavePatternFromWIF_wchar(wcWeaveParameters *params,
        const wchar_t *filename)
{
    WeaveData *data = wif_read_wchar(filename);
    wif_get_pattern(params, data,
        &params->pattern_width, &params->pattern_height,
        &params->pattern_realwidth, &params->pattern_realheight);
    wif_free_weavedata(data);
    wcFinalizeWeaveParameters(params);
}
#endif
#endif

void wcFreeWeavePattern(wcWeaveParameters *params)
{
    if (params->yarn_types) {
        free(params->yarn_types);
    }
    if (params->pattern) {
        free(params->pattern);
    }
}

static float intensityVariation(wcPatternData pattern_data,
    const wcWeaveParameters *params)
{
    if(params->intensity_fineness < 0.001f){
        return 1.f;
    }
    // have index to make a grid of finess*fineness squares 
    // of which to have the same brightness variations.

    uint32_t tindex_x = pattern_data.total_index_x;
    uint32_t tindex_y = pattern_data.total_index_y;

    //Switch X and Y for warp, so that we have the yarn going along y
    if(!pattern_data.warp_above){
        float tmp = tindex_x;
        tindex_x = tindex_y;
        tindex_y = tmp;
    }

    //segment start x,y
    float centerx = tindex_x - (pattern_data.x*0.5f)*pattern_data.width;
    float centery = tindex_y - (pattern_data.y*0.5f)*pattern_data.length;
    
    uint32_t r1 = (uint32_t) ((centerx + tindex_x) 
            * params->intensity_fineness);
    uint32_t r2 = (uint32_t) ((centery + tindex_y) 
            * params->intensity_fineness);
    
    float xi = sampleTEASingle(r1, r2, 8);
    float log_xi = -logf(xi);
    return log_xi < 10.f ? log_xi : 10.f;
}

static void calculateLengthOfSegment(uint8_t warp_above, uint32_t pattern_x,
                uint32_t pattern_y, uint32_t *steps_left,
                uint32_t *steps_right,  uint32_t pattern_width,
                uint32_t pattern_height, PatternEntry *pattern_entries)
{

    uint32_t current_x = pattern_x;
    uint32_t current_y = pattern_y;
    uint32_t *incremented_coord = warp_above ? &current_y : &current_x;
    uint32_t max_size = warp_above ? pattern_height: pattern_width;
    uint32_t initial_coord = warp_above ? pattern_y: pattern_x;
    *steps_right = 0;
    *steps_left  = 0;
    do{
        (*incremented_coord)++;
        if(*incremented_coord == max_size){
            *incremented_coord = 0;
        }
        if((pattern_entries[current_x +
                current_y*pattern_width].warp_above) != warp_above){
            break;
        }
        (*steps_right)++;
    } while(*incremented_coord != initial_coord);

    *incremented_coord = initial_coord;
    do{
        if(*incremented_coord == 0){
            *incremented_coord = max_size;
        }
        (*incremented_coord)--;
        if((pattern_entries[current_x +
                current_y*pattern_width].warp_above) != warp_above){
            break;
        }
        (*steps_left)++;
    } while(*incremented_coord != initial_coord);
}

static float vonMises(float cos_x, float b) {
    // assumes a = 0, b > 0 is a concentration parameter.
    float I0, absB = fabsf(b);
    if (fabsf(b) <= 3.75f) {
        float t = absB / 3.75f;
        t = t * t;
        I0 = 1.0f + t*(3.5156229f + t*(3.0899424f + t*(1.2067492f
            + t*(0.2659732f + t*(0.0360768f + t*0.0045813f)))));
    } else {
        float t = 3.75f / absB;
        I0 = expf(absB) / sqrtf(absB) * (0.39894228f + t*(0.01328592f
            + t*(0.00225319f + t*(-0.00157565f + t*(0.00916281f
            + t*(-0.02057706f + t*(0.02635537f + t*(-0.01647633f
            + t*0.00392377f))))))));
    }

    return expf(b * cos_x) / (2 * M_PI * I0);
}

wcPatternData wcGetPatternData(wcIntersectionData intersection_data,
        const wcWeaveParameters *params)
{
    if(params->pattern == 0){
        wcPatternData data = {0};
        return data;
    }
    float uv_x = intersection_data.uv_x;
    float uv_y = intersection_data.uv_y;
    //Real world scaling.
    //Set repeating uv coordinates.
    //Either set using realworld scale or uvscale parameters.
    float u_scale, v_scale;
    if (params->realworld_uv) {
        //the user parameters uscale, vscale change roles when realworld_uv
        // is true
        //they are then used to tweak the realworld scales
        u_scale = params->uscale/params->pattern_realwidth; 
        v_scale = params->vscale/params->pattern_realheight;
    } else {
        u_scale = params->uscale;
        v_scale = params->vscale;
    }
    float u_repeat = fmod(uv_x*u_scale,1.f);
    float v_repeat = fmod(uv_y*v_scale,1.f);
    //pattern index
    //TODO(Peter): these are new. perhaps they can be used later 
    // to avoid duplicate calculations.
    //TODO(Peter): come up with a better name for these...
    uint32_t total_x = uv_x*u_scale*params->pattern_width;
    uint32_t total_y = uv_y*v_scale*params->pattern_height;

    //TODO(Vidar): Check why this crashes sometimes
    if (u_repeat < 0.f) {
        u_repeat = u_repeat - floor(u_repeat);
    }
    if (v_repeat < 0.f) {
        v_repeat = v_repeat - floor(v_repeat);
    }

    uint32_t pattern_x = (uint32_t)(u_repeat*(float)(params->pattern_width));
    uint32_t pattern_y = (uint32_t)(v_repeat*(float)(params->pattern_height));

    PatternEntry current_point = params->pattern[pattern_x +
        pattern_y*params->pattern_width];        

    //Calculate the size of the segment
    uint32_t steps_left_warp = 0, steps_right_warp = 0;
    uint32_t steps_left_weft = 0, steps_right_weft = 0;
    if (current_point.warp_above) {
        calculateLengthOfSegment(current_point.warp_above, pattern_x,
            pattern_y, &steps_left_warp, &steps_right_warp,
            params->pattern_width, params->pattern_height,
            params->pattern);
    }else{
        calculateLengthOfSegment(current_point.warp_above, pattern_x,
            pattern_y, &steps_left_weft, &steps_right_weft,
            params->pattern_width, params->pattern_height,
            params->pattern);
    }

    //Yarn-segment-local coordinates.
    float l = (steps_left_warp + steps_right_warp + 1.f);
    float y = ((v_repeat*(float)(params->pattern_height) - (float)pattern_y)
            + steps_left_warp)/l;

    float w = (steps_left_weft + steps_right_weft + 1.f);
    float x = ((u_repeat*(float)(params->pattern_width) - (float)pattern_x)
            + steps_left_weft)/w;

    //Rescale x and y to [-1,1]
    x = x*2.f - 1.f;
    y = y*2.f - 1.f;

    //Switch X and Y for warp, so that we always have the yarn
    // cylinder going along the y axis
    if(!current_point.warp_above){
        float tmp1 = x;
        float tmp2 = w;
        x = -y;
        y = tmp1;
        w = l;
        l = tmp2;
    }
  
    //return the results
    wcPatternData ret_data;
	ret_data.yarn_type = current_point.yarn_type;
    ret_data.length = l; 
    ret_data.width  = w; 
    ret_data.x = x; 
    ret_data.y = y; 
    ret_data.warp_above = current_point.warp_above; 
    calculate_segment_uv_and_normal(&ret_data, params, &intersection_data);
    ret_data.total_index_x = total_x; //total x index of wrapped pattern matrix
    ret_data.total_index_y = total_y; //total y index of wrapped pattern matrix
    return ret_data;
}

float wcEvalFilamentSpecular(wcIntersectionData intersection_data,
    wcPatternData data, const wcWeaveParameters *params)
{
    wcVector wi = wcvector(intersection_data.wi_x, intersection_data.wi_y,
        intersection_data.wi_z);
    wcVector wo = wcvector(intersection_data.wo_x, intersection_data.wo_y,
        intersection_data.wo_z);

    if(!data.warp_above){
        float tmp2 = wi.x;
        float tmp3 = wo.x;
        wi.x = -wi.y; wi.y = tmp2;
        wo.x = -wo.y; wo.y = tmp3;
    }
    wcVector H = wcVector_normalize(wcVector_add(wi,wo));

    float v = data.v;
    float y = data.y;

    //TODO(Peter): explain from where these expressions come.
    //compute v from x using (11). Already done. We have it from data.
    //compute u(wi,v,wr) -- u as function of v. using (4)...
    float specular_u = atan2f(-H.z, H.y) + M_PI_2; //plus or minus in last t.
    //TODO(Peter): check that it indeed is just v that should be used 
    //to calculate Gu (6) in Irawans paper.
    //calculate yarn tangent.

    float reflection = 0.f;
    float umax = wc_yarn_type_get_umax(params,data.yarn_type,
		intersection_data.context);
    if (fabsf(specular_u) < umax){
        // Make normal for highlights, uses v and specular_u
        wcVector highlight_normal = wcVector_normalize(wcvector(sinf(v),
                    sinf(specular_u)*cosf(v),
                    cosf(specular_u)*cosf(v)));

        // Make tangent for highlights, uses v and specular_u
        wcVector highlight_tangent = wcVector_normalize(wcvector(0.f, 
                    cosf(specular_u), -sinf(specular_u)));

        //get specular_y, using irawans transformation.
        float specular_y = specular_u/umax;
        // our transformation TODO(Peter): Verify!
        //float specular_y = sinf(specular_u)/sinf(m_umax);

        float delta_x = wc_yarn_type_get_delta_x(params, data.yarn_type,
			intersection_data.context);
        //Clamp specular_y TODO(Peter): change name of m_delta_x to m_delta_h
        specular_y = specular_y < 1.f - delta_x ? specular_y :
            1.f - delta_x;
        specular_y = specular_y > -1.f + delta_x ? specular_y :
            -1.f + delta_x;

        //this takes the role of xi in the irawan paper.
        if (fabsf(specular_y - y) < delta_x) {
            // --- Set Gu, using (6)
            float a = 1.f; //radius of yarn
            float R = 1.f/(sin(umax)); //radius of curvature
            float Gu = a*(R + a*cosf(v)) /(
                wcVector_magnitude(wcVector_add(wi,wo)) *
                fabsf((wcVector_cross(highlight_tangent,H)).x));

            float alpha = wc_yarn_type_get_alpha(params, data.yarn_type,
				intersection_data.context);
            float beta = wc_yarn_type_get_beta(params, data.yarn_type,
				intersection_data.context);
            // --- Set fc
            float cos_x = -wcVector_dot(wi, wo);
            float fc = alpha + vonMises(cos_x, beta);

            // --- Set A
            float widotn = wcVector_dot(wi, highlight_normal);
            float wodotn = wcVector_dot(wo, highlight_normal);
            widotn = (widotn < 0.f) ? 0.f : widotn;   
            wodotn = (wodotn < 0.f) ? 0.f : wodotn;   
            float A = 0.f;
            if(widotn > 0.f && wodotn > 0.f){
                A = 1.f / (4.0 * M_PI) * (widotn*wodotn)/(widotn + wodotn);
                //TODO(Peter): Explain from where the 1/4*PI factor comes from
            }
            float l = 2.f;
            //TODO(Peter): Implement As, -- smoothes the dissapeares of the
            // higlight near the ends. Described in (9)
            reflection = 2.f*l*umax*fc*Gu*A/delta_x;
        }
    }
    return reflection;
}

float wcEvalStapleSpecular(wcIntersectionData intersection_data,
    wcPatternData data, const wcWeaveParameters *params)
{
    wcVector wi = wcvector(intersection_data.wi_x, intersection_data.wi_y,
        intersection_data.wi_z);
    wcVector wo = wcvector(intersection_data.wo_x, intersection_data.wo_y,
        intersection_data.wo_z);

    if(!data.warp_above){
        float tmp2 = wi.x;
        float tmp3 = wo.x;
        wi.x = -wi.y; wi.y = tmp2;
        wo.x = -wo.y; wo.y = tmp3;
    }
    wcVector H = wcVector_normalize(wcVector_add(wi, wo));

    float psi = wc_yarn_type_get_psi(params,data.yarn_type,
		intersection_data.context);

    float u = data.u;
    float x = data.x;
    float D;
    {
        float a = H.y*sinf(u) + H.z*cosf(u);
        D = (H.y*cosf(u)-H.z*sinf(u))/(sqrtf(H.x*H.x + a*a))/
			tanf(psi);
    }
    float reflection = 0.f;
            
    //Plus eller minus i sista termen?
    float specular_v = atan2f(-H.y*sinf(u) - H.z*cosf(u), H.x) + acosf(D);
    //TODO(Vidar): Clamp specular_v, do we need it?
    // Make normal for highlights, uses u and specular_v
    wcVector highlight_normal = wcVector_normalize(wcvector(sinf(specular_v),
        sinf(u)*cosf(specular_v), cosf(u)*cosf(specular_v)));

    if (fabsf(specular_v) < M_PI_2 && fabsf(D) < 1.f) {
        //we have specular reflection
        //get specular_x, using irawans transformation.
        float specular_x = specular_v/M_PI_2;
        // our transformation
        //float specular_x = sinf(specular_v);

        float delta_x = wc_yarn_type_get_delta_x(params,data.yarn_type,
			intersection_data.context);
        float umax = wc_yarn_type_get_umax(params,data.yarn_type,
			intersection_data.context);

        //Clamp specular_x
        specular_x = specular_x < 1.f - delta_x ? specular_x :
            1.f - delta_x;
        specular_x = specular_x > -1.f + delta_x ? specular_x :
            -1.f + delta_x;

        if (fabsf(specular_x - x) < delta_x) {

            float alpha = wc_yarn_type_get_alpha(params,data.yarn_type,
				intersection_data.context);
            float beta  = wc_yarn_type_get_beta( params,data.yarn_type,
				intersection_data.context);

            // --- Set Gv
            float a = 1.f; //radius of yarn
            float R = 1.f/(sin(umax)); //radius of curvature
            float Gv = a*(R + a*cosf(specular_v))/(
                wcVector_magnitude(wcVector_add(wi,wo)) *
                wcVector_dot(highlight_normal,H) * fabsf(sinf(psi)));
            // --- Set fc
            float cos_x = -wcVector_dot(wi, wo);
            float fc = alpha + vonMises(cos_x, beta);
            // --- Set A
            float widotn = wcVector_dot(wi, highlight_normal);
            float wodotn = wcVector_dot(wo, highlight_normal);
            widotn = (widotn < 0.f) ? 0.f : widotn;   
            wodotn = (wodotn < 0.f) ? 0.f : wodotn;   
            //TODO(Vidar): This is where we get the NAN
            float A = 0.f;
            if(widotn > 0.f && wodotn > 0.f){
                A = 1.f / (4.0 * M_PI) * (widotn*wodotn)/(widotn + wodotn);
                //TODO(Peter): Explain from where the 1/4*PI factor comes from
            }
            float w = 2.f;
            reflection = 2.f*w*umax*fc*Gv*A/delta_x;
        }
    }
    return reflection;
}

wcColor wcEvalDiffuse(wcIntersectionData intersection_data,
        wcPatternData data, const wcWeaveParameters *params)
{
    float value = intersection_data.wi_z;

	wcYarnType *yarn_type = params->yarn_types + data.yarn_type;
    if(!yarn_type->color_enabled){
        yarn_type = &params->yarn_types[0];
    }

    wcColor color = {
        yarn_type->color.r * value,
        yarn_type->color.g * value,
        yarn_type->color.b * value
    };
	if(yarn_type->color_texmap){
		color = wc_eval_texmap_color(yarn_type->color_texmap,
            intersection_data.context);
		color.r*=value; color.g*=value; color.b*=value;
	}
    return color;
}

float wcEvalSpecular(wcIntersectionData intersection_data,
        wcPatternData data, const wcWeaveParameters *params)
{
    // Depending on the given psi parameter the yarn is considered
    // staple or filament. They are treated differently in order
    // to work better numerically. 
    float reflection = 0.f;
    if(params->pattern == 0){
        return 0.f;
    }
    float psi = wc_yarn_type_get_psi(params, data.yarn_type,
		intersection_data.context);
    if (psi <= 0.001f) {
        //Filament yarn
        reflection = wcEvalFilamentSpecular(intersection_data, data, params); 
    } else {
        //Staple yarn
        reflection = wcEvalStapleSpecular(intersection_data, data, params); 
    }
	return reflection * params->specular_normalization
        * intensityVariation(data, params);
}

wcColor wcShade(wcIntersectionData intersection_data,
        const wcWeaveParameters *params)
{
    wcPatternData data = wcGetPatternData(intersection_data,params);
    wcColor ret = wcEvalDiffuse(intersection_data,data,params);
    float spec  = wcEvalSpecular(intersection_data,data,params);
    float specular_strength = wc_yarn_type_get_specular_strength(params,
        data.yarn_type,
		intersection_data.context);
    ret.r = ret.r*(1.f-specular_strength) + specular_strength*spec;
    ret.g = ret.g*(1.f-specular_strength) + specular_strength*spec;
    ret.b = ret.b*(1.f-specular_strength) + specular_strength*spec;
    return ret;
}
