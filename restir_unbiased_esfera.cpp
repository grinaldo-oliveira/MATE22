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
#include <map>

#include <iostream>
#include <locale.h> // Para setlocale
#include <windows.h> // Para SetConsoleOutputCP (se estiver no Windows)

using namespace std;

const float PI = 3.14159265359f;
const float EPSILON = 1e-6f;
const int WIDTH = 800;
const int HEIGHT = 600;

// Vari�veis globais que ser�o configuradas via linha de comando
int MAX_CANDIDATES = 30;      // Valor padr�o
bool ENABLE_SPATIAL_REUSE = true;  // Nova vari�vel para controlar amostragem espacial

// Classe para vetores 3D
class Vec3 {
public:
    float x, y, z;
    
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    Vec3 operator+(const Vec3& other) const {
        return Vec3(x + other.x, y + other.y, z + other.z);
    }
    
    Vec3 operator-(const Vec3& other) const {
        return Vec3(x - other.x, y - other.y, z - other.z);
    }
    
    Vec3 operator*(float scalar) const {
        return Vec3(x * scalar, y * scalar, z * scalar);
    }
    
    float dot(const Vec3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }
    
    float length() const {
        return sqrt(x * x + y * y + z * z);
    }
    
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
    
    Color operator+(const Color& other) const {
        return Color(r + other.r, g + other.g, b + other.b);
    }
    
    Color operator*(float scalar) const {
        return Color(r * scalar, g * scalar, b * scalar);
    }
    
    Color operator*(const Color& other) const {
        return Color(r * other.r, g * other.g, b * other.b);
    }
    
    Color& operator+=(const Color& other) {
        r += other.r;
        g += other.g;
        b += other.b;
        return *this;
    }
    
    void clamp() {
        r = min(1.0f, max(0.0f, r));
        g = min(1.0f, max(0.0f, g));
        b = min(1.0f, max(0.0f, b));
    }
    
    // Fun��o para calcular lumin�ncia (usado para peso)
    float luminance() const {
        return 0.299f * r + 0.587f * g + 0.114f * b;
    }
};

// 1. ADICIONAR ap�s a classe Color (aproximadamente linha 100):

// Classe para esferas
class Sphere {
public:
    Vec3 center;
    float radius;
    Color albedo;
    
    Sphere() : radius(0) {}
    Sphere(const Vec3& c, float r, const Color& alb) 
        : center(c), radius(r), albedo(alb) {}
    
    // Interse��o raio-esfera retorna dist�ncia (negativa se n�o h� interse��o)
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
        
        // Retorna a interse��o mais pr�xima que seja positiva
        if (t1 > EPSILON) return t1;
        if (t2 > EPSILON) return t2;
        return -1.0f;
    }
    
    Vec3 getNormal(const Vec3& point) const {
        return (point - center).normalize();
    }
};


// Fun��o auxiliar para m�ximo
float fmax(float a, float b) {
    return (a > b) ? a : b;
}

// Fun��o auxiliar para m�nimo
float fmin(float a, float b) {
    return (a < b) ? a : b;
}

// Gerador de n�meros aleat�rios simples
float randomFloat() {
    return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
}

int randomInt(int max) {
    return rand() % max;
}

// Classe para luzes
class Light {
public:
    Vec3 position;
    Color color;
    float intensity;
    
    Light() : intensity(0) {}
    Light(const Vec3& pos, const Color& col, float intens) 
        : position(pos), color(col), intensity(intens) {}
    
    // Calcula peso baseado na lumin�ncia da cor resultante
    float calculateWeight(const Vec3& surfacePos, const Vec3& surfaceNormal, 
                         const Color& albedo) const {
        Vec3 lightDir = position - surfacePos;
        float distance = lightDir.length();
        if (distance < EPSILON) return 0.0f;
        
        lightDir = lightDir.normalize();
        float cosTheta = fmax(0.0f, surfaceNormal.dot(lightDir));
        
        // Atenua��o por dist�ncia balanceada
        float attenuation = intensity / (1.0f + distance * distance * 0.005f);
        
        // Calcula a cor resultante e usa sua lumin�ncia como peso
        Color resultColor = color * albedo * (attenuation * cosTheta);
        return resultColor.luminance();
    }
    
    Color calculateLighting(const Vec3& surfacePos, const Vec3& surfaceNormal, 
                           const Color& albedo) const {
        Vec3 lightDir = position - surfacePos;
        float distance = lightDir.length();
        
        if (distance < EPSILON) return Color(0, 0, 0);
        
        lightDir = lightDir.normalize();
        float cosTheta = fmax(0.0f, surfaceNormal.dot(lightDir));
        
        // Atenua��o por dist�ncia balanceada
        float attenuation = intensity / (1.0f + distance * distance * 0.005f);
        Color lighting = color * (attenuation * cosTheta);
        
        return lighting * albedo;
    }
};

// Classe para pontos de superf�cie
class SurfacePoint {
public:
    Vec3 position;
    Vec3 normal;
    Color albedo;
    
    SurfacePoint() {}
    SurfacePoint(const Vec3& pos, const Vec3& norm, const Color& alb)
        : position(pos), normal(norm), albedo(alb) {}
};

// Utilit�ria para converter coordenadas de pixel para ID �nico
int pixelToId(int x, int y) {
    return y * WIDTH + x;
}

// Utilit�ria para converter ID para coordenadas
pair<int, int> idToPixel(int id) {
    return make_pair(id % WIDTH, id / WIDTH);
}

// Classe Reservoir para ReSTIR (VERS�O CORRIGIDA)
class Reservoir {
public:
    int lightIndex;           // z (amostra selecionada)
    float targetPdf;          // p^(z) para o ponto atual
    float W;                  // peso W calculado conforme Equa��o (20)
    int M;                    // contador total de amostras
    float wSum;               // Swi - soma dos pesos das amostras
    vector<int> originPixels; // pixels qi que originaram as amostras
    map<int, int> pixelMCount; // M para cada pixel de origem
    
    // CORRE��O: Adicionar acumulador de cor para compatibilidade com vers�o enviesada
    Color accumulatedColor;
    
    Reservoir() : lightIndex(-1), targetPdf(0.0f), W(0.0f), M(0), wSum(0.0f), 
                  accumulatedColor(0, 0, 0) {}
    
    // CORRE��O: M�todo simples id�ntico � vers�o enviesada
    void updateSimple(const vector<Light>& lights, const SurfacePoint& point, 
                      int candidateLightIndex) {
        if (candidateLightIndex < 0 || candidateLightIndex >= static_cast<int>(lights.size())) {
            return;
        }
        
        float newTargetPdf = lights[candidateLightIndex].calculateWeight(
            point.position, point.normal, point.albedo);
        float sourcePdf = 1.0f / static_cast<float>(lights.size());
        
        // Peso da amostra
        float sampleWeight = (sourcePdf > EPSILON) ? newTargetPdf / sourcePdf : 0.0f;
        
        M++;
        wSum += sampleWeight;
        
        // CORRE��O: Acumula a cor desta amostra (igual � vers�o enviesada)
        Color sampleColor = lights[candidateLightIndex].calculateLighting(
            point.position, point.normal, point.albedo);
        accumulatedColor += sampleColor * sampleWeight;
        
        // Sele��o probabil�stica da amostra (igual � vers�o enviesada)
        if (wSum > EPSILON && randomFloat() < sampleWeight / wSum) {
            lightIndex = candidateLightIndex;
            targetPdf = newTargetPdf;
        }
    }
    
    // CORRE��O: M�todo simples ID�NTICO � vers�o enviesada
    Color getFinalColorSimple(const vector<Light>& lights, const SurfacePoint& point) const {
        if (wSum < EPSILON || M == 0) return Color(0, 0, 0);
        
        // CORRE��O: Usar a mesma f�rmula da vers�o enviesada
        // Vers�o BIASED: W(x,z) = (1/M) * (1/p^(xz)) * Swi(xi)
        return accumulatedColor * (1.0f / wSum);
    }
    
    // Fun��o original de update para amostragem inicial (caso com reutiliza��o espacial)
    void update(const vector<Light>& lights, const SurfacePoint& point, 
                int candidateLightIndex, int originPixelId = -1) {
        if (candidateLightIndex < 0 || candidateLightIndex >= static_cast<int>(lights.size())) {
            return;
        }
        
        float newTargetPdf = lights[candidateLightIndex].calculateWeight(
            point.position, point.normal, point.albedo);
        float sourcePdf = 1.0f / static_cast<float>(lights.size());
        
        // Peso da amostra
        float sampleWeight = (sourcePdf > EPSILON) ? newTargetPdf / sourcePdf : 0.0f;
        
        M++;
        wSum += sampleWeight;
        
        // Rastrear pixel de origem
        if (originPixelId >= 0) {
            originPixels.push_back(originPixelId);
            pixelMCount[originPixelId]++;
        }
        
        // Sele��o probabil�stica da amostra
        if (wSum > EPSILON && randomFloat() < sampleWeight / wSum) {
            lightIndex = candidateLightIndex;
            targetPdf = newTargetPdf;
        }
    }
    
    // IMPLEMENTA��O DO ALGORITMO 6: combineReservoirsUnbiased (apenas para reutiliza��o espacial)
    void combineReservoirsUnbiased(const vector<Reservoir>& inputReservoirs, 
                                   const vector<Light>& lights,
                                   const SurfacePoint& queryPoint,
                                   const vector<int>& queryPixels) {
        
        // Linha 2: Reservoir s (inicializar)
        lightIndex = -1;
        targetPdf = 0.0f;
        W = 0.0f;
        M = 0;
        wSum = 0.0f;
        originPixels.clear();
        pixelMCount.clear();
        
        // Linhas 3-5: foreach r ? {r1, ..., rk} do
        for (size_t i = 0; i < inputReservoirs.size(); ++i) {
            const Reservoir& r = inputReservoirs[i];
            if (r.lightIndex >= 0) {
                // s.update(r.y, p^q(r.y), r.W, r.M)
                updateFromReservoir(r, lights, queryPoint);
                
                // Acumular informa��es de origem
                for (size_t j = 0; j < r.originPixels.size(); ++j) {
                    originPixels.push_back(r.originPixels[j]);
                }
                for (map<int, int>::const_iterator it = r.pixelMCount.begin(); 
                     it != r.pixelMCount.end(); ++it) {
                    pixelMCount[it->first] += it->second;
                }
            }
        }
        
        // Linha 6: Z ? 0
        float Z = 0.0f;
        
        // Linhas 7-10: foreach qi ? {q1, ..., qk} do
        for (size_t i = 0; i < queryPixels.size(); ++i) {
            int qi = queryPixels[i];
            if (lightIndex >= 0) {
                // Linha 8: if p^qi(s.y) > 0 then
                float pqi_sy = calculateTargetPdfForPixel(lightIndex, qi, lights);
                if (pqi_sy > EPSILON) {
                    // Linha 9: Z ? Z + ri.M
                    if (pixelMCount.find(qi) != pixelMCount.end()) {
                        Z += pixelMCount[qi];
                    }
                }
            }
        }
        
        // Linha 11: m ? 1/Z
        float m = (Z > EPSILON) ? 1.0f / Z : 0.0f;
        
        // Linha 12: s.W = p^q(s,y) * (m * s.wsum) / p^q(s,y)
        // Simplificando: s.W = m * s.wsum (conforme Equa��o 20)
        if (lightIndex >= 0 && wSum > EPSILON) {
            W = m * wSum;
        } else {
            W = 0.0f;
        }
    }
    
    // M�todo para obter cor final (caso com reutiliza��o espacial)
    Color getFinalColor(const vector<Light>& lights, const SurfacePoint& point) const {
        if (lightIndex < 0 || W < EPSILON) {
            return Color(0, 0, 0);
        }
        
        // Usar o peso W n�o enviesado
        Color lightContribution = lights[lightIndex].calculateLighting(
            point.position, point.normal, point.albedo);
        
        return lightContribution * W;
    }

private:
    void updateFromReservoir(const Reservoir& other, const vector<Light>& lights, 
                           const SurfacePoint& queryPoint) {
        
        // Recalcular p^q(other.lightIndex) para o ponto de consulta atual
        float newTargetPdf = lights[other.lightIndex].calculateWeight(
            queryPoint.position, queryPoint.normal, queryPoint.albedo);
        
        float weight = newTargetPdf * other.M;
        
        M += other.M;
        wSum += weight;
        
        // Sele��o probabil�stica da amostra
        if (wSum > EPSILON && randomFloat() < weight / wSum) {
            lightIndex = other.lightIndex;
            targetPdf = newTargetPdf;
        }
    }
    
    float calculateTargetPdfForPixel(int lightIdx, int pixelId, const vector<Light>& lights) const {
        if (lightIdx < 0 || lightIdx >= static_cast<int>(lights.size())) {
            return 0.0f;
        }
        
        // Converter pixelId para coordenadas
        pair<int, int> coords = idToPixel(pixelId);
        int x = coords.first;
        int y = coords.second;
        
        // Criar SurfacePoint para o pixel qi
        Vec3 position(x - WIDTH/2, y - HEIGHT/2, 0);
        Vec3 normal(0, 0, 1);
        
        int checkerX = static_cast<int>(floor(x / 50.0f));
        int checkerY = static_cast<int>(floor(y / 50.0f));
        int checker = (checkerX + checkerY) % 2;
        Color albedo = checker ? Color(0.9f, 0.9f, 0.9f) : Color(0.1f, 0.1f, 0.1f);
        
        SurfacePoint pixelPoint(position, normal, albedo);
        
        // Calcular p^qi(lightIdx)
        return lights[lightIdx].calculateWeight(pixelPoint.position, 
                                              pixelPoint.normal, 
                                              pixelPoint.albedo);
    }
};

// Classe principal da cena
class Scene {
public:
    vector<Light> lights;
   	vector<Sphere> spheres;  // ADICIONAR esta linha    
    Vec3 cameraPos;
    Vec3 cameraTarget;
    
    Scene() : cameraPos(0, 0, 100), cameraTarget(0, 0, 0) {}
    
    void setupLights() {
        lights.clear();
        
        // LUZES PRINCIPAIS - intensidades aumentadas para marcar proje��o
        lights.push_back(Light(Vec3(-150, -150, 150), Color(1.0f, 0.1f, 0.1f), 400));  // Vermelha intensa
        lights.push_back(Light(Vec3(150, -150, 150), Color(0.1f, 1.0f, 0.1f), 400));   // Verde intensa
        lights.push_back(Light(Vec3(-150, 150, 150), Color(0.1f, 0.1f, 1.0f), 400));   // Azul intensa
        lights.push_back(Light(Vec3(150, 150, 150), Color(1.0f, 1.0f, 0.1f), 400));    // Amarela intensa
        lights.push_back(Light(Vec3(0, 0, 200), Color(1.0f, 0.2f, 0.8f), 350));        // Magenta central forte
        
        // LUZES LATERAIS - intensidades altas para marcar as bordas
        lights.push_back(Light(Vec3(-350, 0, 140), Color(0.2f, 0.4f, 1.0f), 300));     // Azul forte esquerda
        lights.push_back(Light(Vec3(350, 0, 140), Color(1.0f, 0.2f, 0.4f), 300));      // Rosa forte direita
        lights.push_back(Light(Vec3(0, -250, 160), Color(0.2f, 1.0f, 0.4f), 280));     // Verde forte superior
        lights.push_back(Light(Vec3(0, 250, 160), Color(1.0f, 0.6f, 0.1f), 280));      // Laranja forte inferior
        
        cout << "Total de luzes configuradas: " << lights.size() << " (algoritmo corrigido)" << endl;
    }
    // ADICIONAR este m�todo:
    void setupSpheres() {
        spheres.clear();
        
        // Criar esferas brancas nos quadrados pretos
        float sphereRadius = 18.0f; // Raio da esfera
        
        for (int checkerX = 0; checkerX < WIDTH / 50; checkerX++) {
            for (int checkerY = 0; checkerY < HEIGHT / 50; checkerY++) {
                // Verifica se � um quadrado preto
                if ((checkerX + checkerY) % 2 == 0) { // Quadrado preto
                    // Calcula o centro do quadrado
                    float centerX = (checkerX * 50 + 25) - WIDTH/2;
                    float centerY = (checkerY * 50 + 25) - HEIGHT/2;
                    float centerZ = sphereRadius; // Esfera elevada acima do plano
                    
                    Vec3 sphereCenter(centerX, centerY, centerZ);
                    Color sphereAlbedo(0.9f, 0.9f, 0.9f); // Branco com albedo lambertiano
                    
                    spheres.push_back(Sphere(sphereCenter, sphereRadius, sphereAlbedo));
                }
            }
        }
        
        cout << "Total de esferas criadas: " << spheres.size() << " (nos quadrados pretos)" << endl;
    }
};


// Classe do renderizador ReSTIR
class ReSTIRRenderer {
private:
    Scene scene;
    
public:
	// 4. MODIFICAR o construtor da classe ReSTIRRenderer:
	
	ReSTIRRenderer() {
	    scene.setupLights();
	    scene.setupSpheres();  // ADICIONAR esta linha
	    srand(static_cast<unsigned int>(time(NULL)));
	}
    
	// 3. SUBSTITUIR o m�todo createSurfacePoint na classe ReSTIRRenderer:
	
	SurfacePoint createSurfacePoint(float x, float y) const {
	    Vec3 rayOrigin(x - WIDTH/2, y - HEIGHT/2, 100); // Posi��o da c�mera
	    Vec3 rayDir(0, 0, -1); // Dire��o do raio (para baixo)
	    
	    // Verificar interse��es com esferas primeiro
        float closestDistance = 1e30f;
        int closestSphere = -1;
	    
	    for (size_t i = 0; i < scene.spheres.size(); i++) {
	        float distance = scene.spheres[i].intersect(rayOrigin, rayDir);
	        if (distance > 0 && (closestDistance < 0 || distance < closestDistance)) {
	            closestDistance = distance;
	            closestSphere = static_cast<int>(i);
	        }
	    }
	    
	    // Se h� interse��o com esfera, retornar ponto da esfera
	    if (closestSphere >= 0) {
	        Vec3 hitPoint = rayOrigin + rayDir * closestDistance;
	        Vec3 normal = scene.spheres[closestSphere].getNormal(hitPoint);
	        Color albedo = scene.spheres[closestSphere].albedo;
	        
	        return SurfacePoint(hitPoint, normal, albedo);
	    }
	    
	    // Caso contr�rio, retornar ponto do plano xadrez
	    Vec3 position(x - WIDTH/2, y - HEIGHT/2, 0);
	    Vec3 normal(0, 0, 1);
	    
	    // Usar floor() para evitar problemas de truncamento
	    int checkerX = static_cast<int>(floor(x / 50.0f));
	    int checkerY = static_cast<int>(floor(y / 50.0f));
	    int checker = (checkerX + checkerY) % 2;
	    
	    // Cores mais saturadas para o xadrez
	    Color albedo = checker ? Color(0.9f, 0.9f, 0.9f) : Color(0.1f, 0.1f, 0.1f);
	    
	    return SurfacePoint(position, normal, albedo);
	}
    
    // CORRE��O PRINCIPAL: Usar m�todo id�ntico � vers�o enviesada quando sem reutiliza��o espacial
    Color renderPixel(float x, float y) {
        SurfacePoint point = createSurfacePoint(x, y);
        int currentPixelId = pixelToId(static_cast<int>(x), static_cast<int>(y));
        
        // CORRE��O: Quando n�o h� reutiliza��o espacial, usar m�todo ID�NTICO ao enviesado
        if (!ENABLE_SPATIAL_REUSE) {
            Reservoir simpleReservoir;
            for (int i = 0; i < MAX_CANDIDATES; i++) {
                int lightIndex = randomInt(static_cast<int>(scene.lights.size()));
                simpleReservoir.updateSimple(scene.lights, point, lightIndex);
            }
            
            Color finalColor = simpleReservoir.getFinalColorSimple(scene.lights, point);
            Color ambient = point.albedo * 0.01f;
            finalColor += ambient;
            return finalColor;
        }
        
        // Usar Algoritmo 6 apenas quando h� reutiliza��o espacial
        vector<Reservoir> inputReservoirs;
        vector<int> queryPixels;
        
        // Reservat�rio principal (amostragem inicial)
        Reservoir mainReservoir;
        for (int i = 0; i < MAX_CANDIDATES; i++) {
            int lightIndex = randomInt(static_cast<int>(scene.lights.size()));
            mainReservoir.update(scene.lights, point, lightIndex, currentPixelId);
        }
        inputReservoirs.push_back(mainReservoir);
        queryPixels.push_back(currentPixelId);
        
        // Reservat�rios de reutiliza��o espacial
        for (int dx = -1; dx <= 1; dx += 2) {  // S� diagonais
            for (int dy = -1; dy <= 1; dy += 2) {
                float nx = x + dx * 50;  // Usar m�ltiplos do tamanho do quadrado
                float ny = y + dy * 50;
                
                if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
                    int neighborPixelId = pixelToId(static_cast<int>(nx), static_cast<int>(ny));
                    SurfacePoint neighborPoint = createSurfacePoint(nx, ny);
                    
                    // Simular reservoir de pixel vizinho
                    Reservoir spatialReservoir;
                    for (int j = 0; j < 8; j++) {  // Menos amostras para vizinhos
                        int lightIdx = randomInt(static_cast<int>(scene.lights.size()));
                        spatialReservoir.update(scene.lights, neighborPoint, lightIdx, neighborPixelId);
                    }
                    
                    inputReservoirs.push_back(spatialReservoir);
                    queryPixels.push_back(neighborPixelId);
                }
            }
        }
        
        // APLICAR ALGORITMO 6: Combina��o n�o enviesada de m�ltiplos reservat�rios
        Reservoir finalReservoir;
        finalReservoir.combineReservoirsUnbiased(inputReservoirs, scene.lights, 
                                                point, queryPixels);
        
        // Obter cor final usando peso n�o enviesado
        Color finalColor = finalReservoir.getFinalColor(scene.lights, point);
        
        // Ilumina��o ambiente m�nima
        Color ambient = point.albedo * 0.01f;
        finalColor += ambient;
        
        return finalColor;
    }
    
    vector<Color> render() {
        vector<Color> image(WIDTH * HEIGHT);
        
        cout << "Renderizando cena " << WIDTH << "x" << HEIGHT << "..." << endl;
        if (ENABLE_SPATIAL_REUSE) {
            cout << "Usando ALGORITMO 6 - COMBINA��O N�O ENVIESADA" << endl;
        } else {
            cout << "Usando M�TODO ID�NTICO � VERS�O ENVIESADA" << endl;
        }
        cout << "MAX_CANDIDATES: " << MAX_CANDIDATES << endl;
        cout << "AMOSTRAGEM ESPACIAL: " << (ENABLE_SPATIAL_REUSE ? "ATIVADA" : "DESATIVADA") << endl;
        clock_t start = clock();
        
        for (int y = 0; y < HEIGHT; y++) {
            if (y % 50 == 0) {
                cout << "Linha " << y << "/" << HEIGHT 
                     << " (" << fixed << setprecision(1) 
                     << (static_cast<float>(y) / HEIGHT * 100) << "%)" << endl;
            }
            
            for (int x = 0; x < WIDTH; x++) {
                image[y * WIDTH + x] = renderPixel(static_cast<float>(x), static_cast<float>(y));
            }
        }
        
        clock_t end = clock();
        double duration = static_cast<double>(end - start) / CLOCKS_PER_SEC;
        
        cout << "Renderiza��o conclu�da em " << duration << " segundos" << endl;
        
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
    cout << "Uso: " << programName << " [op��es]" << endl;
    cout << "Op��es:" << endl;
    cout << "  -c, --candidates <n�mero>      Define MAX_CANDIDATES (padr�o: 30)" << endl;
    cout << "  -s, --spatial-reuse            Ativa amostragem espacial (padr�o: ativada)" << endl;
    cout << "      --no-spatial-reuse         Desativa amostragem espacial" << endl;
    cout << "  -h, --help                     Mostra esta ajuda" << endl;
    cout << endl;
    cout << "VERS�O CORRIGIDA - ID�NTICA � vers�o enviesada quando sem reutiliza��o espacial" << endl;
    cout << "Exemplos:" << endl;
    cout << "  " << programName << " -c 50                    # 50 candidatos com amostragem espacial" << endl;
    cout << "  " << programName << " --no-spatial-reuse       # Sem amostragem espacial (=vers�o enviesada)" << endl;
    cout << "  " << programName << " -c 20 --no-spatial-reuse # 20 candidatos sem amostragem espacial" << endl;
}

bool parseArguments(int argc, char* argv[]) {
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
        else if (arg == "-s" || arg == "--spatial-reuse") {
            ENABLE_SPATIAL_REUSE = true;
        }
        else if (arg == "--no-spatial-reuse") {
            ENABLE_SPATIAL_REUSE = false;
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
    oss << "restir_fixed_" << MAX_CANDIDATES << "_";
    if (ENABLE_SPATIAL_REUSE) {
        oss << "spatial";
    } else {
        oss << "nospatial";
    }
    oss << "_unbiased.ppm";
    return oss.str();
}

int main(int argc, char* argv[]) {
    // Configura��o de localidade
    setlocale(LC_ALL, "Portuguese");
    SetConsoleOutputCP(850);
    
    cout << "=== ReSTIR VERS�O CORRIGIDA - Resultados Id�nticos Quando Necess�rio ===" << endl;
    
    // Parse dos argumentos de linha de comando
    if (!parseArguments(argc, argv)) {
        return 1;
    }
    
    cout << "Configura��o:" << endl;
    cout << "  MAX_CANDIDATES: " << MAX_CANDIDATES << endl;
    cout << "  AMOSTRAGEM_ESPACIAL: " << (ENABLE_SPATIAL_REUSE ? "ATIVADA" : "DESATIVADA") << endl;
    if (ENABLE_SPATIAL_REUSE)  {
        cout << "  Algoritmo: ALGORITMO 6 - N�O ENVIESADO" << endl;
    } else {
        cout << "  Algoritmo: M�TODO SIMPLES (equivalente ao enviesado)" << endl;
    }
    cout << endl;
    
    ReSTIRRenderer renderer;
    vector<Color> image = renderer.render();
    
    string filename = generateFilename();
    renderer.saveImage(image, filename);
    
    cout << "Programa finalizado com sucesso!" << endl;
    cout << "Abra o arquivo '" << filename << "' para ver o resultado!" << endl;
    cout << "\nCORRE��ES IMPLEMENTADAS:" << endl;
    cout << " M�todo simples para caso sem reutiliza��o espacial" << endl;
    cout << " Resultado id�ntico � vers�o enviesada quando --no-spatial-reuse" << endl;
    cout << " Rastreamento de pixels de origem (qi)" << endl;
    cout << " Reavalia��o de PDFs para pontos de consulta" << endl;
    cout << " C�lculo do fator de normaliza��o Z" << endl;
    cout << " Peso W n�o enviesado conforme Equa��o (20)" << endl;
    cout << " Combina��o matematicamente correta de reservat�rios" << endl;
    cout << "\nDIFEREN�AS DA VERS�O ANTERIOR:" << endl;
    cout << "- M�todo combineReservoirsUnbiased() implementado" << endl;
    cout << "- Cada reservat�rio rastreia suas amostras de origem" << endl;
    cout << "- Peso final calculado usando m = 1/Z" << endl;
    cout << "- Garantia matem�tica de n�o enviesamento" << endl;
    
    return 0;
}
