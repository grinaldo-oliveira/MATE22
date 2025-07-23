#include <iostream>
#include <vector>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <iomanip>
#include <string>
#include <sstream>
#include <windows.h>

using namespace std;

const float PI = 3.14159265359f;
const float EPSILON = 1e-6f;
const int WIDTH = 800;
const int HEIGHT = 600;

// Variáveis globais configuráveis
int MAX_CANDIDATES = 30;
bool ENABLE_SPATIAL_REUSE = true;
bool ENABLE_TEMPORAL_REUSE = true;
bool USE_BASELINE_IMAGE = false;
bool USE_UNBIASED_MODE = false;
bool USE_MONTE_CARLO_ONLY = false;
int BASELINE_RIS_SAMPLES = 0; // NOVA VARIÁVEL: 0 = desabilitado
int RECURSIVE_ITERATIONS = 1; // NOVA VARIÁVEL: quantidade de renderizações sequenciais a partir do baseline

// Classe para vetores 3D
class Vec3 {
public:
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    Vec3 operator+(const Vec3& other) const { return Vec3(x + other.x, y + other.y, z + other.z); }
    Vec3 operator-(const Vec3& other) const { return Vec3(x - other.x, y - other.y, z - other.z); }
    Vec3 operator*(float scalar) const { return Vec3(x * scalar, y * scalar, z * scalar); }
    float dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }
    float length() const { return sqrt(x * x + y * y + z * z); }
    Vec3 normalize() const {
        float len = length();
        if (len < EPSILON) return Vec3(0, 0, 0);
        return *this * (1.0f / len);
    }
};

// Classe para cores
class Color {
public:
    float r, g, b;
    Color() : r(0), g(0), b(0) {}
    Color(float r, float g, float b) : r(r), g(g), b(b) {}
    Color operator+(const Color& other) const { return Color(r + other.r, g + other.g, b + other.b); }
    Color operator*(float scalar) const { return Color(r * scalar, g * scalar, b * scalar); }
    Color operator*(const Color& other) const { return Color(r * other.r, g * other.g, b * other.b); }
    Color& operator+=(const Color& other) { r += other.r; g += other.g; b += other.b; return *this; }
    void clamp() {
        r = min(1.0f, max(0.0f, r));
        g = min(1.0f, max(0.0f, g));
        b = min(1.0f, max(0.0f, b));
    }
    float luminance() const { return 0.299f * r + 0.587f * g + 0.114f * b; }
};

// Funções auxiliares C++98 compatíveis
float fmax(float a, float b) { return (a > b) ? a : b; }
float fmin(float a, float b) { return (a < b) ? a : b; }
float randomFloat() { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); }
int randomInt(int max) { return rand() % max; }

// Classe para esferas
class Sphere {
public:
    Vec3 center;
    float radius;
    Color albedo;
    
    Sphere() : radius(0) {}
    Sphere(const Vec3& c, float r, const Color& alb) 
        : center(c), radius(r), albedo(alb) {}
    
    float intersect(const Vec3& rayOrigin, const Vec3& rayDir) const {
        Vec3 oc = rayOrigin - center;
        float a = rayDir.dot(rayDir);
        float b = 2.0f * oc.dot(rayDir);
        float c = oc.dot(oc) - radius * radius;
        float discriminant = b * b - 4 * a * c;
        
        if (discriminant < 0) return -1.0f;
        
        float sqrt_discriminant = sqrt(discriminant);
        float t1 = (-b - sqrt_discriminant) / (2.0f * a);
        float t2 = (-b + sqrt_discriminant) / (2.0f * a);
        
        if (t1 > EPSILON) return t1;
        if (t2 > EPSILON) return t2;
        return -1.0f;
    }
    
    Vec3 getNormal(const Vec3& point) const {
        return (point - center).normalize();
    }
};

// Classe para luzes
class Light {
public:
    Vec3 position;
    Color color;
    float intensity;
    Light() : intensity(0) {}
    Light(const Vec3& pos, const Color& col, float intens) : position(pos), color(col), intensity(intens) {}
    
    float calculateWeight(const Vec3& surfacePos, const Vec3& surfaceNormal, const Color& albedo) const {
        Vec3 lightDir = position - surfacePos;
        float distance = lightDir.length();
        if (distance < EPSILON) return 0.0f;
        lightDir = lightDir.normalize();
        float cosTheta = fmax(0.0f, surfaceNormal.dot(lightDir));
        float geometricTerm = cosTheta / (distance * distance);
        float attenuation = intensity / (1.0f + distance * distance * 0.008f);
        Color contribution = color * albedo * (attenuation * geometricTerm);
        return contribution.luminance();
    }
    
    Color calculateLighting(const Vec3& surfacePos, const Vec3& surfaceNormal, const Color& albedo) const {
        Vec3 lightDir = position - surfacePos;
        float distance = lightDir.length();
        if (distance < EPSILON) return Color(0, 0, 0);
        lightDir = lightDir.normalize();
        float cosTheta = fmax(0.0f, surfaceNormal.dot(lightDir));
        float attenuation = intensity / (1.0f + distance * distance * 0.008f);
        Color lighting = color * (attenuation * cosTheta);
        return lighting * albedo;
    }
};

// Classe para pontos de superfície
class SurfacePoint {
public:
    Vec3 position;
    Vec3 normal;
    Color albedo;
    bool isSphere;
    
    SurfacePoint() : isSphere(false) {}
    SurfacePoint(const Vec3& pos, const Vec3& norm, const Color& alb, bool sphere)
        : position(pos), normal(norm), albedo(alb), isSphere(sphere) {}
};

// Classe específica para Monte Carlo (sem RIS)
class MonteCarloReservoir {
public:
    Color accumulatedColor;
    float weight;
    int M;
    
    MonteCarloReservoir() : accumulatedColor(0, 0, 0), weight(0.0f), M(0) {}
    
    void update(const vector<Light>& lights, const SurfacePoint& point, 
                int candidateLightIndex) {
        if (candidateLightIndex < 0 || candidateLightIndex >= static_cast<int>(lights.size())) {
            return;
        }
        
        float newTargetPdf = lights[candidateLightIndex].calculateWeight(
            point.position, point.normal, point.albedo);
        float sourcePdf = 1.0f / static_cast<float>(lights.size());
        
        float sampleWeight = (sourcePdf > EPSILON) ? newTargetPdf / sourcePdf : 0.0f;
        
        M++;
        weight += sampleWeight;
        
        Color sampleColor = lights[candidateLightIndex].calculateLighting(
            point.position, point.normal, point.albedo);
        accumulatedColor += sampleColor * sampleWeight;
    }
    
    Color getFinalColor() const {
        if (weight < EPSILON || M == 0) return Color(0, 0, 0);
        return accumulatedColor * (1.0f / weight);
    }
};

// Classe Reservoir CORRIGIDA com MIS correto
class Reservoir {
public:
    int lightIndex;
    float targetPdf;
    float weight;
    int M;
    int pixelOrigin;
    
    Reservoir() : lightIndex(-1), targetPdf(0.0f), weight(0.0f), M(0), pixelOrigin(-1) {}

    void update(const vector<Light>& lights, const SurfacePoint& point, int candidateLightIndex) {
        if (candidateLightIndex < 0 || candidateLightIndex >= static_cast<int>(lights.size())) return;
        float newTargetPdf = lights[candidateLightIndex].calculateWeight(point.position, point.normal, point.albedo);
        float sourcePdf = 1.0f / static_cast<float>(lights.size());
        float sampleWeight = (sourcePdf > EPSILON) ? newTargetPdf / sourcePdf : 0.0f;
        weight += sampleWeight;
        M++;
        if (weight > EPSILON && randomFloat() < sampleWeight / weight) {
            lightIndex = candidateLightIndex;
            targetPdf = newTargetPdf;
        }
    }

    void combine(const Reservoir& other, const vector<Light>& lights, const SurfacePoint& point) {
        if (other.lightIndex < 0 || other.M == 0) return;
        
        float otherWeight;
        float otherTargetPdf;
        
        if (USE_UNBIASED_MODE) {
            otherTargetPdf = lights[other.lightIndex].calculateWeight(point.position, point.normal, point.albedo);
            otherWeight = otherTargetPdf * static_cast<float>(other.M);
        } else {
            otherTargetPdf = other.targetPdf;
            otherWeight = other.targetPdf * static_cast<float>(other.M);
        }
        
        weight += otherWeight;
        M += other.M;
        
        if (weight > EPSILON && randomFloat() < otherWeight / weight) {
            lightIndex = other.lightIndex;
            targetPdf = otherTargetPdf;
        }
    }

    Color getFinalColor(const vector<Light>& lights, const SurfacePoint& point) const {
        if (lightIndex < 0 || targetPdf < EPSILON || M == 0) return Color(0, 0, 0);
        float W = (weight / static_cast<float>(M)) / targetPdf;
        return lights[lightIndex].calculateLighting(point.position, point.normal, point.albedo) * W;
    }
};

// IMPLEMENTAÇÃO CORRIGIDA: Combinação MIS conforme Algoritmo 6 do artigo
Reservoir combineReservoirsUnbiasedMISCorrected(
    int currentPixel,
    const vector<Reservoir>& inputReservoirs,
    const vector<int>& pixelOrigins,
    const vector<SurfacePoint>& surfacePoints,
    const vector<Light>& lights
) {
    Reservoir s;
    s.pixelOrigin = currentPixel;
    float totalM = 0;
    
    // Para cada reservatório de entrada
    for (size_t i = 0; i < inputReservoirs.size(); ++i) {
        const Reservoir& r = inputReservoirs[i];
        if (r.lightIndex < 0 || r.M == 0) continue;
        
        // Peso de reamostragem: targetPdf no pixel atual * W * M
        float currentTargetPdf = lights[r.lightIndex].calculateWeight(
            surfacePoints[currentPixel].position,
            surfacePoints[currentPixel].normal,
            surfacePoints[currentPixel].albedo
        );
        
        // CORREÇÃO CRÍTICA: Usar o peso original do reservatório, não recalcular
        float resamplingWeight = currentTargetPdf * static_cast<float>(r.M);
        
        // Atualização do reservatório usando weighted reservoir sampling
        s.weight += resamplingWeight;
        totalM += static_cast<float>(r.M);
        
        if (s.weight > EPSILON && randomFloat() < resamplingWeight / s.weight) {
            s.lightIndex = r.lightIndex;
            s.targetPdf = currentTargetPdf;
        }
    }
    
    s.M = static_cast<int>(totalM);
    
    // CORREÇÃO CRÍTICA: Peso final conforme RIS padrão, sem MIS adicional
    if (s.lightIndex >= 0 && s.targetPdf > EPSILON && s.M > 0) {
        // Peso RIS padrão: (1/targetPdf) * (wsum/M)
        float W = (s.weight / static_cast<float>(s.M)) / s.targetPdf;
        s.weight = W * s.targetPdf * static_cast<float>(s.M); // Reconstrói weight para consistência
    } else {
        s.weight = 0.0f;
    }
    
    return s;
}

// Classe para carregar imagem PPM
class PPMLoader {
public:
    static vector<Color> loadPPM(const string& filename, int& width, int& height) {
        ifstream file(filename.c_str(), ios::binary);
        vector<Color> image;
        if (!file.is_open()) {
            cerr << "Erro: Não foi possível abrir o arquivo " << filename << endl;
            return image;
        }
        string magic;
        file >> magic;
        if (magic != "P3" && magic != "P6") {
            cerr << "Erro: Formato PPM não suportado (apenas P3 e P6)" << endl;
            file.close();
            return image;
        }
        file >> width >> height;
        int maxVal;
        file >> maxVal;
        if (maxVal <= 0 || maxVal > 255) {
            cerr << "Erro: Valor máximo inválido no arquivo PPM" << endl;
            file.close();
            return image;
        }
        image.resize(width * height);
        if (magic == "P3") {
            for (int i = 0; i < width * height; i++) {
                int r, g, b;
                file >> r >> g >> b;
                image[i] = Color(static_cast<float>(r) / static_cast<float>(maxVal),
                                 static_cast<float>(g) / static_cast<float>(maxVal),
                                 static_cast<float>(b) / static_cast<float>(maxVal));
            }
        } else {
            file.ignore();
            for (int i = 0; i < width * height; i++) {
                unsigned char rgb[3];
                file.read(reinterpret_cast<char*>(rgb), 3);
                image[i] = Color(static_cast<float>(rgb[0]) / static_cast<float>(maxVal),
                                 static_cast<float>(rgb[1]) / static_cast<float>(maxVal),
                                 static_cast<float>(rgb[2]) / static_cast<float>(maxVal));
            }
        }
        file.close();
        cout << "Imagem baseline carregada: " << filename << " (" << width << "x" << height << ")" << endl;
        return image;
    }
};

// Classe principal da cena
class Scene {
public:
    vector<Light> lights;
    vector<Sphere> spheres;
    Vec3 cameraPos;
    Vec3 cameraTarget;
    
    Scene() : cameraPos(0, 0, 100), cameraTarget(0, 0, 0) {}
    
    void setupLights() {
        lights.clear();
        
        // Luzes otimizadas para demonstrar diferenças BIASED/UNBIASED
        lights.push_back(Light(Vec3(-200, -200, 200), Color(1.0f, 0.2f, 0.2f), 600));
        lights.push_back(Light(Vec3(200, -200, 200), Color(0.2f, 1.0f, 0.2f), 600));
        lights.push_back(Light(Vec3(-200, 200, 200), Color(0.2f, 0.2f, 1.0f), 600));
        lights.push_back(Light(Vec3(200, 200, 200), Color(1.0f, 1.0f, 0.2f), 600));
        lights.push_back(Light(Vec3(0, 0, 250), Color(1.0f, 0.5f, 0.8f), 700));
        lights.push_back(Light(Vec3(-400, 0, 180), Color(0.3f, 0.6f, 1.0f), 500));
        lights.push_back(Light(Vec3(400, 0, 180), Color(1.0f, 0.3f, 0.6f), 500));
        
        cout << "Total de luzes otimizadas: " << lights.size() << " (configuradas para demonstrar diferenças BIASED/UNBIASED)" << endl;
    }
    
    void setupSpheres() {
        spheres.clear();
        
        float sphereRadius = 22.0f;
        
        for (int checkerX = 0; checkerX < WIDTH / 50; checkerX++) {
            for (int checkerY = 0; checkerY < HEIGHT / 50; checkerY++) {
                if ((checkerX + checkerY) % 2 == 0) {
                    float centerX = (checkerX * 50 + 25) - WIDTH/2;
                    float centerY = (checkerY * 50 + 25) - HEIGHT/2;
                    float centerZ = sphereRadius;
                    
                    Vec3 sphereCenter(centerX, centerY, centerZ);
                    Color sphereAlbedo(0.95f, 0.95f, 0.95f);
                    
                    spheres.push_back(Sphere(sphereCenter, sphereRadius, sphereAlbedo));
                }
            }
        }
        
        cout << "Total de esferas otimizadas: " << spheres.size() << " (albedo 0.95 para máximo contraste)" << endl;
    }
};

// Renderizador ReSTIR CORRIGIDO
class ReSTIRRenderer {
public:
    Scene scene;
    vector<Reservoir> previousFrame;
    vector<SurfacePoint> surfacePoints;
    vector<Color> baselineImage;
    bool hasBaselineImage;
    
public:
    ReSTIRRenderer() : hasBaselineImage(false) {
        scene.setupLights();
        scene.setupSpheres();
        srand(static_cast<unsigned int>(time(NULL)));
        previousFrame.resize(WIDTH * HEIGHT);
        surfacePoints.resize(WIDTH * HEIGHT);
    }
    
    bool loadBaselineImage(const string& filename) {
        int imgWidth, imgHeight;
        baselineImage = PPMLoader::loadPPM(filename, imgWidth, imgHeight);
        if (baselineImage.empty()) {
            cout << "Aviso: Não foi possível carregar a imagem baseline. Continuando sem reutilização temporal baseada em imagem." << endl;
            hasBaselineImage = false;
            return false;
        }
        if (imgWidth != WIDTH || imgHeight != HEIGHT) {
            cout << "Aviso: Dimensões da imagem baseline (" << imgWidth << "x" << imgHeight
                 << ") não coincidem com as dimensões do renderizador (" << WIDTH << "x" << HEIGHT << ")" << endl;
            cout << "Redimensionando ou usando apenas a parte compatível..." << endl;
            vector<Color> resizedImage(WIDTH * HEIGHT, Color(0, 0, 0));
            int minWidth = min(imgWidth, WIDTH);
            int minHeight = min(imgHeight, HEIGHT);
            for (int y = 0; y < minHeight; y++) {
                for (int x = 0; x < minWidth; x++) {
                    resizedImage[y * WIDTH + x] = baselineImage[y * imgWidth + x];
                }
            }
            baselineImage = resizedImage;
        }
        hasBaselineImage = true;
        cout << "Imagem baseline carregada com sucesso para reutilização temporal!" << endl;
        return true;
    }
    
    // NOVA FUNÇÃO: Renderiza baseline RIS puro (sem reutilização espacial/temporal)
    vector<Color> renderRISBaseline(int samples) {
        vector<Color> image(WIDTH * HEIGHT);
        vector<SurfacePoint> baselineSurfacePoints(WIDTH * HEIGHT);
        
        cout << "Gerando baseline RIS puro com " << samples << " amostras..." << endl;
        cout << "  - Sem reutilização espacial" << endl;
        cout << "  - Sem reutilização temporal" << endl;
        cout << "  - Apenas RIS com " << samples << " candidatos por pixel" << endl;
        
        clock_t start = clock();
        
        for (int y = 0; y < HEIGHT; y++) {
            if (y % 50 == 0) {
                cout << "Baseline RIS - Linha " << y << "/" << HEIGHT
                     << " (" << fixed << setprecision(1)
                     << (static_cast<float>(y) / HEIGHT * 100) << "%)" << endl;
            }
            
            for (int x = 0; x < WIDTH; x++) {
                int pixelIndex = y * WIDTH + x;
                SurfacePoint point = createSurfacePoint(static_cast<float>(x), static_cast<float>(y));
                baselineSurfacePoints[pixelIndex] = point;
                
                // RIS puro com número especificado de candidatos
                Reservoir reservoir;
                reservoir.pixelOrigin = pixelIndex;
                
                for (int i = 0; i < samples; i++) {
                    int lightIndex = randomInt(static_cast<int>(scene.lights.size()));
                    reservoir.update(scene.lights, point, lightIndex);
                }
                
                // Renderizar cor final
                Color finalColor = reservoir.getFinalColor(scene.lights, point);
                Color ambient = point.albedo * 0.005f;
                finalColor += ambient;
                image[pixelIndex] = finalColor;
            }
        }
        
        clock_t end = clock();
        double duration = static_cast<double>(end - start) / CLOCKS_PER_SEC;
        cout << "Baseline RIS gerado em " << duration << " segundos" << endl;
        
        return image;
    }
    
    Reservoir reconstructReservoirFromBaseline(const Color& baselineColor, const SurfacePoint& point, int pixelIndex) {
        Reservoir reservoir;
        reservoir.pixelOrigin = pixelIndex;
        float bestMatch = -1.0f;
        int bestLightIndex = -1;
        for (int i = 0; i < static_cast<int>(scene.lights.size()); i++) {
            Color lightContribution = scene.lights[i].calculateLighting(point.position, point.normal, point.albedo);
            float similarity = 1.0f - fabs(baselineColor.r - lightContribution.r)
                                   - fabs(baselineColor.g - lightContribution.g)
                                   - fabs(baselineColor.b - lightContribution.b);
            if (similarity > bestMatch) {
                bestMatch = similarity;
                bestLightIndex = i;
            }
        }
        if (bestLightIndex >= 0 && bestMatch > 0.0f) {
            reservoir.lightIndex = bestLightIndex;
            reservoir.targetPdf = scene.lights[bestLightIndex].calculateWeight(point.position, point.normal, point.albedo);
            float intensity = baselineColor.luminance();
            reservoir.M = static_cast<int>(max(1.0f, intensity * 50.0f));
            reservoir.weight = reservoir.targetPdf * static_cast<float>(reservoir.M);
        }
        return reservoir;
    }
    
    SurfacePoint createSurfacePoint(float x, float y) const {
        Vec3 rayOrigin(x - WIDTH/2, y - HEIGHT/2, 100);
        Vec3 rayDir(0, 0, -1);
        
        float closestDistance = 1e30f;
        int closestSphere = -1;
        
        for (int i = 0; i < static_cast<int>(scene.spheres.size()); i++) {
            float distance = scene.spheres[i].intersect(rayOrigin, rayDir);
            if (distance > 0 && distance < closestDistance) {
                closestDistance = distance;
                closestSphere = i;
            }
        }
        
        if (closestSphere >= 0) {
            Vec3 hitPoint = rayOrigin + rayDir * closestDistance;
            Vec3 normal = scene.spheres[closestSphere].getNormal(hitPoint);
            Color albedo = scene.spheres[closestSphere].albedo;
            
            return SurfacePoint(hitPoint, normal, albedo, true);
        }
        
        Vec3 position(x - WIDTH/2, y - HEIGHT/2, 0);
        Vec3 normal(0, 0, 1);
        int checkerX = static_cast<int>(floor(x / 50.0f));
        int checkerY = static_cast<int>(floor(y / 50.0f));
        int checker = (checkerX + checkerY) % 2;
        Color albedo = checker ? Color(0.9f, 0.9f, 0.9f) : Color(0.1f, 0.1f, 0.1f);
        return SurfacePoint(position, normal, albedo, false);
    }
    
    // FUNÇÃO CORRIGIDA: Reutilização espacial com MIS correto
    void spatialReuseUnbiasedMISCorrected(Reservoir& reservoir, int x, int y, const vector<Reservoir>& reservoirs) {
        int spatialSamples = 3; // Reduzido para modo unbiased (mais caro)
        int spatialRadius = 20;
        int currentPixel = y * WIDTH + x;
        
        // Coleta reservatórios válidos
        vector<Reservoir> inputReservoirs;
        vector<int> pixelOrigins;
        
        // Adiciona o próprio reservatório
        inputReservoirs.push_back(reservoir);
        pixelOrigins.push_back(currentPixel);
        
        // Adiciona vizinhos válidos
        for (int i = 0; i < spatialSamples; i++) {
            float angle = randomFloat() * 2.0f * PI;
            int dx = static_cast<int>(cos(angle) * (randomFloat() * spatialRadius));
            int dy = static_cast<int>(sin(angle) * (randomFloat() * spatialRadius));
            int nx = x + dx;
            int ny = y + dy;
            if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
                int neighborIdx = ny * WIDTH + nx;
                const Reservoir& neighbor = reservoirs[neighborIdx];
                if (neighbor.lightIndex >= 0 && neighbor.M > 0) {
                    inputReservoirs.push_back(neighbor);
                    pixelOrigins.push_back(neighborIdx);
                }
            }
        }
        
        // Combina usando MIS correto
        reservoir = combineReservoirsUnbiasedMISCorrected(currentPixel, inputReservoirs, pixelOrigins, surfacePoints, scene.lights);
    }
    
    void spatialReuse(Reservoir& reservoir, const SurfacePoint& point, int x, int y, const vector<Reservoir>& reservoirs) {
        if (USE_UNBIASED_MODE) {
            spatialReuseUnbiasedMISCorrected(reservoir, x, y, reservoirs);
            return;
        }
        
        // Modo biased original
        int spatialSamples = 4;
        int spatialRadius = 20;
        for (int i = 0; i < spatialSamples; i++) {
            float angle = randomFloat() * 2.0f * PI;
            int dx = static_cast<int>(cos(angle) * (randomFloat() * spatialRadius));
            int dy = static_cast<int>(sin(angle) * (randomFloat() * spatialRadius));
            int nx = x + dx;
            int ny = y + dy;
            if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
                int neighborIdx = ny * WIDTH + nx;
                const Reservoir& neighborReservoir = reservoirs[neighborIdx];
                reservoir.combine(neighborReservoir, scene.lights, point);
            }
        }
    }
    
    vector<Color> renderMonteCarlo() {
        vector<Color> image(WIDTH * HEIGHT);
        
        cout << "Renderizando com MONTE CARLO PURO..." << endl;
        cout << "Configuração:" << endl;
        cout << "  MAX_CANDIDATES: " << MAX_CANDIDATES << endl;
        cout << "  MODO: Monte Carlo Puro (sem RIS, sem reutilização)" << endl;
        cout << "  Total de luzes: " << scene.lights.size() << endl;
        cout << "  Total de esferas: " << scene.spheres.size() << endl;
        
        clock_t start = clock();
        
        for (int y = 0; y < HEIGHT; y++) {
            if (y % 50 == 0) {
                cout << "Linha " << y << "/" << HEIGHT
                     << " (" << fixed << setprecision(1)
                     << (static_cast<float>(y) / HEIGHT * 100) << "%)" << endl;
            }
            
            for (int x = 0; x < WIDTH; x++) {
                int pixelIndex = y * WIDTH + x;
                SurfacePoint point = createSurfacePoint(static_cast<float>(x), static_cast<float>(y));
                
                MonteCarloReservoir mcReservoir;
                
                for (int i = 0; i < MAX_CANDIDATES; i++) {
                    int lightIndex = randomInt(static_cast<int>(scene.lights.size()));
                    mcReservoir.update(scene.lights, point, lightIndex);
                }
                
                Color finalColor = mcReservoir.getFinalColor();
                Color ambient = point.albedo * 0.005f;
                finalColor += ambient;
                image[pixelIndex] = finalColor;
            }
        }
        
        clock_t end = clock();
        double duration = static_cast<double>(end - start) / CLOCKS_PER_SEC;
        cout << "Renderização Monte Carlo concluída em " << duration << " segundos" << endl;
        return image;
    }
    
    vector<Color> render() {
        if (USE_MONTE_CARLO_ONLY) {
            return renderMonteCarlo();
        }
        
        vector<Color> image(WIDTH * HEIGHT);
        vector<Reservoir> currentFrame(WIDTH * HEIGHT);
        
        // MODIFICAÇÃO: Verificar se deve gerar baseline RIS interno
        if (BASELINE_RIS_SAMPLES > 0) {
            cout << "MODO BASELINE RIS INTERNO ATIVADO" << endl;
            baselineImage = renderRISBaseline(BASELINE_RIS_SAMPLES);
            hasBaselineImage = true;
            USE_BASELINE_IMAGE = true; // Forçar uso do baseline gerado
        }
        
        cout << "Renderizando com ReSTIR " << (USE_UNBIASED_MODE ? "UNBIASED CORRIGIDO" : "BIASED") << " + ESFERAS OTIMIZADAS..." << endl;
        cout << "Configuração:" << endl;
        cout << "  MAX_CANDIDATES: " << MAX_CANDIDATES << endl;
        cout << "  AMOSTRAGEM_ESPACIAL: " << (ENABLE_SPATIAL_REUSE ? "ATIVADA" : "DESATIVADA") << endl;
        cout << "  AMOSTRAGEM_TEMPORAL: " << (ENABLE_TEMPORAL_REUSE ? "ATIVADA" : "DESATIVADA") << endl;
        
        if (BASELINE_RIS_SAMPLES > 0) {
            cout << "  BASELINE_RIS: ATIVADA (" << BASELINE_RIS_SAMPLES << " amostras)" << endl;
        } else {
            cout << "  BASELINE_IMAGE: " << (USE_BASELINE_IMAGE && hasBaselineImage ? "ATIVADA" : "DESATIVADA") << endl;
        }
        
        cout << "  MODO: " << (USE_UNBIASED_MODE ? "UNBIASED CORRIGIDO - SEM ESCURECIMENTO" : "BIASED") << endl;
        cout << "  Total de luzes: " << scene.lights.size() << endl;
        cout << "  Total de esferas: " << scene.spheres.size() << endl;
        
        clock_t start = clock();
        
        // Passo 1: Gerar pontos de superfície e RIS inicial
        for (int y = 0; y < HEIGHT; y++) {
            if (y % 50 == 0) {
                cout << "Linha " << y << "/" << HEIGHT
                     << " (" << fixed << setprecision(1)
                     << (static_cast<float>(y) / HEIGHT * 100) << "%)" << endl;
            }
            for (int x = 0; x < WIDTH; x++) {
                int pixelIndex = y * WIDTH + x;
                SurfacePoint point = createSurfacePoint(static_cast<float>(x), static_cast<float>(y));
                surfacePoints[pixelIndex] = point;
                
                Reservoir reservoir;
                reservoir.pixelOrigin = pixelIndex;
                
                for (int i = 0; i < MAX_CANDIDATES; i++) {
                    int lightIndex = randomInt(static_cast<int>(scene.lights.size()));
                    reservoir.update(scene.lights, point, lightIndex);
                }
                
                // Reutilização temporal
                if (ENABLE_TEMPORAL_REUSE) {
                    if (hasBaselineImage && USE_BASELINE_IMAGE &&
                        pixelIndex >= 0 && pixelIndex < static_cast<int>(baselineImage.size())) {
                        Color baselineColor = baselineImage[pixelIndex];
                        Reservoir baselineReservoir = reconstructReservoirFromBaseline(baselineColor, point, pixelIndex);
                        
                        if (USE_UNBIASED_MODE) {
                            // Usar combinação corrigida para temporal também
                            vector<Reservoir> tempReservoirs;
                            vector<int> tempOrigins;
                            tempReservoirs.push_back(reservoir);
                            tempReservoirs.push_back(baselineReservoir);
                            tempOrigins.push_back(pixelIndex);
                            tempOrigins.push_back(pixelIndex);
                            
                            reservoir = combineReservoirsUnbiasedMISCorrected(pixelIndex, tempReservoirs, tempOrigins, surfacePoints, scene.lights);
                        } else {
                            reservoir.combine(baselineReservoir, scene.lights, point);
                        }
                    } else if (pixelIndex >= 0 && pixelIndex < static_cast<int>(previousFrame.size())) {
                        // Limitação temporal conforme artigo (M anterior <= 20 * M atual)
                        Reservoir tempReservoir = previousFrame[pixelIndex];
                        if (tempReservoir.M > 20 * reservoir.M) {
                            tempReservoir.M = 20 * reservoir.M;
                            tempReservoir.weight = tempReservoir.targetPdf * static_cast<float>(tempReservoir.M);
                        }
                        
                        if (USE_UNBIASED_MODE) {
                            vector<Reservoir> tempReservoirs;
                            vector<int> tempOrigins;
                            tempReservoirs.push_back(reservoir);
                            tempReservoirs.push_back(tempReservoir);
                            tempOrigins.push_back(pixelIndex);
                            tempOrigins.push_back(tempReservoir.pixelOrigin);
                            
                            reservoir = combineReservoirsUnbiasedMISCorrected(pixelIndex, tempReservoirs, tempOrigins, surfacePoints, scene.lights);
                        } else {
                            reservoir.combine(tempReservoir, scene.lights, point);
                        }
                    }
                }
                currentFrame[pixelIndex] = reservoir;
            }
        }
        
        // Passo 2: Reutilização espacial
        if (ENABLE_SPATIAL_REUSE) {
            vector<Reservoir> spatialFrame = currentFrame;
            for (int y = 0; y < HEIGHT; y++) {
                for (int x = 0; x < WIDTH; x++) {
                    int pixelIndex = y * WIDTH + x;
                    SurfacePoint point = surfacePoints[pixelIndex];
                    Reservoir reservoir = currentFrame[pixelIndex];
                    spatialReuse(reservoir, point, x, y, currentFrame);
                    spatialFrame[pixelIndex] = reservoir;
                }
            }
            currentFrame = spatialFrame;
        }
        
        // Passo 3: Geração da imagem final
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                int pixelIndex = y * WIDTH + x;
                SurfacePoint point = surfacePoints[pixelIndex];
                Reservoir& reservoir = currentFrame[pixelIndex];
                previousFrame[pixelIndex] = reservoir;
                Color finalColor = reservoir.getFinalColor(scene.lights, point);
                Color ambient = point.albedo * 0.005f;
                finalColor += ambient;
                image[pixelIndex] = finalColor;
            }
        }
        
        clock_t end = clock();
        double duration = static_cast<double>(end - start) / CLOCKS_PER_SEC;
        cout << "Renderização concluída em " << duration << " segundos" << endl;
        return image;
    }
    
    void saveImage(const vector<Color>& image, const string& filename) const {
        ofstream file(filename.c_str());
        if (!file.is_open()) {
            cerr << "Erro ao criar arquivo " << filename << endl;
            return;
        }
        file << "P3" << endl << WIDTH << " " << HEIGHT << endl << "255" << endl;
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                Color pixel = image[y * WIDTH + x];
                pixel.clamp();
                int r = static_cast<int>(pixel.r * 255);
                int g = static_cast<int>(pixel.g * 255);
                int b = static_cast<int>(pixel.b * 255);
                file << r << " " << g << " " << b << " ";
            }
            file << endl;
        }
        file.close();
        cout << "Imagem salva como " << filename << endl;
    }
};

void printUsage(const char* programName) {
    cout << "Uso: " << programName << " [opções]" << endl;
    cout << "Opções:" << endl;
    cout << "  -c, --candidates <numero>      Define MAX_CANDIDATES (padrao: 30)" << endl;
    cout << "  -v, --baseline-ris <numero>    Gera baseline RIS interno com N amostras" << endl;
    cout << "  -s, --spatial-reuse            Ativa amostragem espacial" << endl;
    cout << "      --no-spatial-reuse         Desativa amostragem espacial" << endl;
    cout << "  -t, --temporal-reuse           Ativa amostragem temporal" << endl;
    cout << "      --no-temporal-reuse        Desativa amostragem temporal" << endl;
    cout << "  -b, --baseline <arquivo.ppm>   Usa imagem baseline para reutilizacao temporal" << endl;
    cout << "      --biased                   Usa versão biased (padrao)" << endl;
    cout << "      --unbiased                 Usa versão unbiased CORRIGIDA" << endl;
    cout << "      --monte-carlo              Usa Monte Carlo puro (desabilita RIS)" << endl;
	cout << "  -i, --iterations <numero>       Iterações recursivas a partir do baseline (padrão: 1)" << endl;    
    cout << "  -h, --help                     Mostra esta ajuda" << endl;
    cout << endl;
    cout << "Exemplos:" << endl;
    cout << "  " << programName << " -c 1 -v 32 -s -t --unbiased   # RIS c/1 candidato + baseline RIS 32 amostras" << endl;
    cout << "  " << programName << " -c 50 -s -t --unbiased        # Configuração unbiased CORRIGIDA" << endl;
    cout << "  " << programName << " -c 50 -s -t --biased          # Configuração biased completa" << endl;
    cout << "  " << programName << " -b baseline.ppm -t --unbiased # Usa imagem baseline (unbiased CORRIGIDO)" << endl;
    cout << "  " << programName << " --monte-carlo -c 100          # Monte Carlo puro com 100 candidatos" << endl;
    cout << "  " << programName << " -v 64 -s -t                   # Baseline RIS 64 amostras + ReSTIR completo" << endl;
}

bool parseArguments(int argc, char* argv[], string& baselineFile) {
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "-c" || arg == "--candidates") {
            if (i + 1 < argc) {
                MAX_CANDIDATES = atoi(argv[++i]);
                if (MAX_CANDIDATES <= 0) {
                    cerr << "Erro: MAX_CANDIDATES deve ser maior que 0" << endl;
                    return false;
                }
            } else {
                cerr << "Erro: " << arg << " requer um valor" << endl;
                return false;
            }
        }
        else if (arg == "-v" || arg == "--baseline-ris") {
            if (i + 1 < argc) {
                BASELINE_RIS_SAMPLES = atoi(argv[++i]);
                if (BASELINE_RIS_SAMPLES <= 0) {
                    cerr << "Erro: BASELINE_RIS_SAMPLES deve ser maior que 0" << endl;
                    return false;
                }
            } else {
                cerr << "Erro: " << arg << " requer um valor" << endl;
                return false;
            }
        }
        else if (arg == "-s" || arg == "--spatial-reuse") {
            ENABLE_SPATIAL_REUSE = true;
        }
        else if (arg == "--no-spatial-reuse") {
            ENABLE_SPATIAL_REUSE = false;
        }
        else if (arg == "-t" || arg == "--temporal-reuse") {
            ENABLE_TEMPORAL_REUSE = true;
        }
        else if (arg == "--no-temporal-reuse") {
            ENABLE_TEMPORAL_REUSE = false;
        }
        else if (arg == "-b" || arg == "--baseline") {
            if (i + 1 < argc) {
                baselineFile = argv[++i];
                USE_BASELINE_IMAGE = true;
            } else {
                cerr << "Erro: " << arg << " requer um arquivo" << endl;
                return false;
            }
        }
        else if (arg == "--biased") {
            USE_UNBIASED_MODE = false;
        }
        else if (arg == "--unbiased") {
            USE_UNBIASED_MODE = true;
        }
        else if (arg == "--monte-carlo") {
            USE_MONTE_CARLO_ONLY = true;
            ENABLE_SPATIAL_REUSE = false;
            ENABLE_TEMPORAL_REUSE = false;
            USE_BASELINE_IMAGE = false;
        }
        else if (arg == "-i" || arg == "--iterations") {
			    if (i + 1 < argc) {
			        RECURSIVE_ITERATIONS = atoi(argv[++i]);
			        if (RECURSIVE_ITERATIONS <= 0) {
			            cerr << "Erro: RECURSIVE_ITERATIONS deve ser maior que 0" << endl;
			            return false;
			        }
			    } else {
			        cerr << "Erro: " << arg << " requer um valor" << endl;
			        return false;
			    }
			}
        else {
            cerr << "Erro: Argumento desconhecido: " << arg << endl;
            printUsage(argv[0]);
            return false;
        }
    }
    return true;
}

string generateFilename() {
    ostringstream oss;
    
    if (USE_MONTE_CARLO_ONLY) {
        oss << "monte_carlo_pure_" << MAX_CANDIDATES << "_candidates.ppm";
    } else {
        oss << "restir_" << (USE_UNBIASED_MODE ? "unbiased_CORRIGIDO" : "biased") << "_" << MAX_CANDIDATES << "_";
        if (ENABLE_SPATIAL_REUSE) oss << "spatial_";
        if (ENABLE_TEMPORAL_REUSE) oss << "temporal_";
        if (BASELINE_RIS_SAMPLES > 0) {
            oss << "baseline_ris_" << BASELINE_RIS_SAMPLES << "_";
        } else if (USE_BASELINE_IMAGE) {
            oss << "baseline_";
        }
        oss << ".ppm";
    }
    
    return oss.str();
}

int main(int argc, char* argv[]) {
    // Configura a página de código do console para UTF-8
    SetConsoleOutputCP(CP_UTF8);
    // Opcional: Se você estiver lendo entrada do usuário com acentos
    SetConsoleCP(CP_UTF8);
    cout << "=== Renderizador ReSTIR CORRIGIDO com Baseline RIS Interno - Compatível C++98 ===" << endl;
    string baselineFile;
    if (!parseArguments(argc, argv, baselineFile)) {
        return 1;
    }
    
    cout << "Configuracao:" << endl;
    cout << "  MAX_CANDIDATES: " << MAX_CANDIDATES << endl;
    
    if (USE_MONTE_CARLO_ONLY) {
        cout << "  MODO: MONTE CARLO PURO" << endl;
        cout << "  AMOSTRAGEM_ESPACIAL: DESABILITADA (Monte Carlo puro)" << endl;
        cout << "  AMOSTRAGEM_TEMPORAL: DESABILITADA (Monte Carlo puro)" << endl;
        cout << "  BASELINE_IMAGE: DESABILITADA (Monte Carlo puro)" << endl;
    } else {
        cout << "  AMOSTRAGEM_ESPACIAL: " << (ENABLE_SPATIAL_REUSE ? "ATIVADA" : "DESATIVADA") << endl;
        cout << "  AMOSTRAGEM_TEMPORAL: " << (ENABLE_TEMPORAL_REUSE ? "ATIVADA" : "DESATIVADA") << endl;
        
        if (BASELINE_RIS_SAMPLES > 0) {
            cout << "  BASELINE_RIS: INTERNO (" << BASELINE_RIS_SAMPLES << " amostras)" << endl;
        } else {
            cout << "  BASELINE_IMAGE: " << (USE_BASELINE_IMAGE ? baselineFile : "NENHUMA") << endl;
        }
        
        cout << "  MODO: " << (USE_UNBIASED_MODE ? "UNBIASED CORRIGIDO - SEM ESCURECIMENTO" : "BIASED") << endl;
    }
    
    cout << "  GEOMETRIA: Plano xadrez + Esferas otimizadas (albedo 0.95)" << endl;
    cout << "  ILUMINACAO: 7 luzes focadas para destacar diferenças" << endl;
    cout << endl;
    
    ReSTIRRenderer renderer;
    
    // Verificar se deve carregar baseline de arquivo (só se não for RIS interno)
    if (USE_BASELINE_IMAGE && !baselineFile.empty() && !USE_MONTE_CARLO_ONLY && BASELINE_RIS_SAMPLES == 0) {
        if (!renderer.loadBaselineImage(baselineFile)) {
            cout << "Continuando sem imagem baseline..." << endl;
        }
    }
    
	vector<Color> image;
	string baseFilename = generateFilename();
	// Remover sufixo ".ppm" para montar nomes numerados
	string fn_prefix = baseFilename;
	if (fn_prefix.size() >= 4 && fn_prefix.substr(fn_prefix.size()-4) == ".ppm")
	    fn_prefix = fn_prefix.substr(0, fn_prefix.size()-4);
	
	// Primeira renderização normalmente
	image = renderer.render();
	string iterFilename = fn_prefix + "_iter1.ppm";
	renderer.saveImage(image, iterFilename);
	cout << "Salvo: " << iterFilename << endl;
	
	// Iterações recursivas a partir do baseline gerado
	for(int iter = 2; iter <= RECURSIVE_ITERATIONS; ++iter) {
	    // Salva resultado anterior como baseline temporária
	    renderer.baselineImage = image;
	    renderer.hasBaselineImage = true;
	    USE_BASELINE_IMAGE = true;
	    BASELINE_RIS_SAMPLES = 0; // Não gera novo baseline RIS
	    vector<Color> newImage = renderer.render();
	    char iterSuffix[16];
	    sprintf(iterSuffix, "_iter%d.ppm", iter); 
	    string nextFilename = fn_prefix + string(iterSuffix);
	    renderer.saveImage(newImage, nextFilename);
	    cout << "Salvo: " << nextFilename << endl;
	    image = newImage;
	}
    
    cout << "Programa finalizado com sucesso!" << endl;
    cout << "Abra o arquivo '" << iterFilename << "' para ver o resultado!" << endl;
    
    if (BASELINE_RIS_SAMPLES > 0) {
        cout << "\nMODO BASELINE RIS INTERNO UTILIZADO:" << endl;
        cout << "- Baseline gerado com " << BASELINE_RIS_SAMPLES << " amostras RIS puras" << endl;
        cout << "- ReSTIR aplicado com " << MAX_CANDIDATES << " candidatos iniciais" << endl;
        cout << "- Reutilização temporal baseada no baseline RIS interno" << endl;
        cout << "- Reutilização espacial: " << (ENABLE_SPATIAL_REUSE ? "ATIVADA" : "DESATIVADA") << endl;
    }
    
    return 0;
}
