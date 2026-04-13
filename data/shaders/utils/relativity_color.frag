bool relativityVisualsEnabled()
{
    return u_relativity_params.x > 0.5;
}

bool dopplerVisualsEnabled()
{
    return u_relativity_params.y > 0.5;
}

// OpenRelativity color shift constants
#define xla 0.39952807612909519
#define xlb 444.63156780935032
#define xlc 20.095464678736523

#define xha 1.1305579611401821
#define xhb 593.23109262398259
#define xhc 34.446036241271742

#define ya 1.0098874822455657
#define yb 556.03724875218927
#define yc 46.184868454550838

#define za 2.0648400466720593
#define zb 448.45126344558236
#define zc 22.357297606503543

#define IR_RANGE 400.0
#define IR_START 700.0
#define UV_RANGE 380.0
#define UV_START 0.0

vec3 RGBToXYZC(float r, float g, float b)
{
    vec3 xyz;
    xyz.x = 0.13514*r + 0.120432*g + 0.057128*b;
    xyz.y = 0.0668999*r + 0.232706*g + 0.0293946*b;
    xyz.z = 0.0*r + 0.0000218959*g + 0.358278*b;
    return xyz;
}

vec3 XYZToRGBC(float x, float y, float z)
{
    vec3 rgb;
    rgb.x = 9.94845*x - 5.1485*y - 1.16389*z;
    rgb.y = -2.86007*x + 5.77745*y - 0.0179627*z;
    rgb.z = 0.000174791*x - 0.000353084*y + 2.79113*z;
    return rgb;
}

vec3 weightFromXYZCurves(vec3 xyz)
{
    vec3 returnVal;
    returnVal.x = 0.0735806 * xyz.x - 0.0380793 * xyz.y - 0.00860837 * xyz.z;
    returnVal.y = -0.0665378 * xyz.x + 0.134408 * xyz.y - 0.000417865 * xyz.z;
    returnVal.z = 0.00000299624 * xyz.x - 0.00000605249 * xyz.y + 0.0484424 * xyz.z;
    return returnVal;
}

float getXFromCurve(vec3 param, float shift)
{
    float top1 = param.x * xla * exp(-(pow((param.y*shift) - xlb, 2.0)/(2.0*(pow(param.z*shift, 2.0)+pow(xlc, 2.0))))) * sqrt(2.0*3.14159265358979323);
    float bottom1 = sqrt(1.0/pow(param.z*shift, 2.0) + 1.0/pow(xlc, 2.0)); 

    float top2 = param.x * xha * exp(-(pow((param.y*shift) - xhb, 2.0)/(2.0*(pow(param.z*shift, 2.0)+pow(xhc, 2.0))))) * sqrt(2.0*3.14159265358979323);
    float bottom2 = sqrt(1.0/pow(param.z*shift, 2.0) + 1.0/pow(xhc, 2.0));

    return (top1/bottom1) + (top2/bottom2);
}

float getYFromCurve(vec3 param, float shift)
{
    float top = param.x * ya * exp(-(pow((param.y*shift) - yb, 2.0)/(2.0*(pow(param.z*shift, 2.0)+pow(yc, 2.0))))) * sqrt(2.0*3.14159265358979323);
    float bottom = sqrt(1.0/pow(param.z*shift, 2.0) + 1.0/pow(yc, 2.0)); 
    return top/bottom;
}

float getZFromCurve(vec3 param, float shift)
{
    float top = param.x * za * exp(-(pow((param.y*shift) - zb, 2.0)/(2.0*(pow(param.z*shift, 2.0)+pow(zc, 2.0))))) * sqrt(2.0*3.14159265358979323);
    float bottom = sqrt(1.0/pow(param.z*shift, 2.0) + 1.0/pow(zc, 2.0));
    return top/bottom;
}

vec3 constrainRGB(float r, float g, float b)
{
    float w;
    w = (0.0 < r) ? 0.0 : r;
    w = (w < g) ? w : g;
    w = (w < b) ? w : b;
    w = -w;
    
    if (w > 0.0) {
        r += w;  g += w; b += w;
    }
    w = r;
    w = (w < g) ? g : w;
    w = (w < b) ? b : w;

    if (w > 1.0)
    {
        r /= w;
        g /= w;
        b /= w;
    }	
    return vec3(r, g, b);
}

vec3 applyDopplerShift(vec3 color, vec3 view_dir)
{
    if (!dopplerVisualsEnabled()) return color;

    vec3 beta = u_relativity_beta.xyz;
    float beta2 = dot(beta, beta);
    if (beta2 < 1e-6 || beta2 >= 1.0) return color;

    float gamma = clamp(u_relativity_params.z, 1.0, 100.0);
    float shift = gamma * (1.0 - dot(beta, view_dir));
    
    if (shift < 0.01 || shift > 100.0) return color;
    if (shift > 0.999 && shift < 1.001) return color;
    
    vec3 xyz = RGBToXYZC(color.r, color.g, color.b);
    if (any(isnan(xyz)) || any(isinf(xyz))) return color;

    vec3 weights = weightFromXYZCurves(xyz);
    vec3 rParam = vec3(weights.x, 615.0, 8.0);
    vec3 gParam = vec3(weights.y, 550.0, 4.0);
    vec3 bParam = vec3(weights.z, 463.0, 5.0);
    vec3 UVParam = vec3(0.02, UV_START + UV_RANGE*0.0, 5.0);
    vec3 IRParam = vec3(0.02, IR_START + IR_RANGE*0.0, 5.0);
    
    float invShift = 1.0 / shift;
    float shift3 = invShift * invShift * invShift;
    
    float xf = shift3 * (getXFromCurve(rParam, shift) + getXFromCurve(gParam, shift) + getXFromCurve(bParam, shift) + getXFromCurve(IRParam, shift) + getXFromCurve(UVParam, shift));
    float yf = shift3 * (getYFromCurve(rParam, shift) + getYFromCurve(gParam, shift) + getYFromCurve(bParam, shift) + getYFromCurve(IRParam, shift) + getYFromCurve(UVParam, shift));
    float zf = shift3 * (getZFromCurve(rParam, shift) + getZFromCurve(gParam, shift) + getZFromCurve(bParam, shift) + getZFromCurve(IRParam, shift) + getZFromCurve(UVParam, shift));
    
    if (isnan(xf) || isnan(yf) || isnan(zf) || isinf(xf) || isinf(yf) || isinf(zf)) return color;

    vec3 rgbFinal = XYZToRGBC(xf, yf, zf);
    return constrainRGB(rgbFinal.x, rgbFinal.y, rgbFinal.z);
}
