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

#include <iostream>
#include <locale.h> // Para setlocale
#include <windows.h> // Para SetConsoleOutputCP (se estiver no Windows)

using namespace std;

const float PI = 3.14159265359f;
const float EPSILON = 1e-6f;
const int WIDTH = 800;
const int HEIGHT = 600;

// Variáveis globais que serão configuradas via linha de comando
int MAX_CANDIDATES = 30;      // Valor padrão
bool ENABLE_SPATIAL_REUSE = true;  // Nova variável para controlar amostragem espacial

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
    
    // Função para calcular luminância (usado para peso)
    float luminance() const {
        return 0.299f * r + 0.587f * g + 0.114f * b;
    }
};

// Função auxiliar para máximo
float fmax(float a, float b) {
    return (a > b) ? a : b;
}

// Função auxiliar para mínimo
float fmin(float a, float b) {
    return (a < b) ? a : b;
}

// Gerador de números aleatórios simples
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
    
    // Calcula peso baseado na luminância da cor resultante
    float calculateWeight(const Vec3& surfacePos, const Vec3& surfaceNormal, 
                         const Color& albedo) const {
        Vec3 lightDir = position - surfacePos;
        float distance = lightDir.length();
        if (distance < EPSILON) return 0.0f;
        
        lightDir = lightDir.normalize();
        float cosTheta = fmax(0.0f, surfaceNormal.dot(lightDir));
        
        // Atenuação por distância balanceada
        float attenuation = intensity / (1.0f + distance * distance * 0.005f);
        
        // Calcula a cor resultante e usa sua luminância como peso
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
        
        // Atenuação por distância balanceada
        float attenuation = intensity / (1.0f + distance * distance * 0.005f);
        Color lighting = color * (attenuation * cosTheta);
        
        return lighting * albedo;
    }
};

// Classe para esferas
class Sphere {
public:
    Vec3 center;
    float radius;
    Color albedo;
    
    Sphere() : radius(0) {}
    Sphere(const Vec3& c, float r, const Color& alb) 
        : center(c), radius(r), albedo(alb) {}
    
    // Interseção raio-esfera retorna distância (negativa se não há interseção)
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
        
        // Retorna a interseção mais próxima que seja positiva
        if (t1 > EPSILON) return t1;
        if (t2 > EPSILON) return t2;
        return -1.0f;
    }
    
    Vec3 getNormal(const Vec3& point) const {
        return (point - center).normalize();
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
    SurfacePoint(const Vec3& pos, const Vec3& norm, const Color& alb, bool sphere = false)
        : position(pos), normal(norm), albedo(alb), isSphere(sphere) {}
};

// Classe Reservoir para ReSTIR (apenas versão BIASED)
class Reservoir {
public:
    int lightIndex;
    float targetPdf;
    float weight;
    int M;
    Color accumulatedColor;
    
    Reservoir() : lightIndex(-1), targetPdf(0.0f), weight(0.0f), M(0), 
                  accumulatedColor(0, 0, 0) {}
    
    void update(const vector<Light>& lights, const SurfacePoint& point, 
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
        weight += sampleWeight;
        
        // Acumula a cor desta amostra
        Color sampleColor = lights[candidateLightIndex].calculateLighting(
            point.position, point.normal, point.albedo);
        accumulatedColor += sampleColor * sampleWeight;
        
        // Seleção probabilística da amostra
        if (weight > EPSILON && randomFloat() < sampleWeight / weight) {
            lightIndex = candidateLightIndex;
            targetPdf = newTargetPdf;
        }
    }
    
    void combine(const Reservoir& other, const vector<Light>& lights, 
                 const SurfacePoint& point) {
        if (other.lightIndex < 0) return;
        
        float newTargetPdf = lights[other.lightIndex].calculateWeight(
            point.position, point.normal, point.albedo);
        float combinedWeight = newTargetPdf * other.M;
        
        M += other.M;
        weight += combinedWeight;
        accumulatedColor += other.accumulatedColor;
        
        if (weight > EPSILON && randomFloat() < combinedWeight / weight) {
            lightIndex = other.lightIndex;
            targetPdf = newTargetPdf;
        }
    }
    
    Color getFinalColor() const {
        if (weight < EPSILON || M == 0) return Color(0, 0, 0);
        
        // Versão BIASED: W(x,z) = (1/M) * (1/p^(xz)) * Swi(xi)
        return accumulatedColor * (1.0f / weight);
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
        
        // LUZES PRINCIPAIS - mantidas nas mesmas posições e cores
        lights.push_back(Light(Vec3(-150, -150, 150), Color(1.0f, 0.1f, 0.1f), 400));  // Vermelha intensa
        lights.push_back(Light(Vec3(150, -150, 150), Color(0.1f, 1.0f, 0.1f), 400));   // Verde intensa
        lights.push_back(Light(Vec3(-150, 150, 150), Color(0.1f, 0.1f, 1.0f), 400));   // Azul intensa
        lights.push_back(Light(Vec3(150, 150, 150), Color(1.0f, 1.0f, 0.1f), 400));    // Amarela intensa
        lights.push_back(Light(Vec3(0, 0, 200), Color(1.0f, 0.2f, 0.8f), 350));        // Magenta central forte
        
        // LUZES LATERAIS - mantidas nas mesmas posições e cores
        lights.push_back(Light(Vec3(-350, 0, 140), Color(0.2f, 0.4f, 1.0f), 300));     // Azul forte esquerda
        lights.push_back(Light(Vec3(350, 0, 140), Color(1.0f, 0.2f, 0.4f), 300));      // Rosa forte direita
        lights.push_back(Light(Vec3(0, -250, 160), Color(0.2f, 1.0f, 0.4f), 280));     // Verde forte superior
        lights.push_back(Light(Vec3(0, 250, 160), Color(1.0f, 0.6f, 0.1f), 280));      // Laranja forte inferior
        
        cout << "Total de luzes configuradas: " << lights.size() << " (intensas para projeção)" << endl;
    }
    
    void setupSpheres() {
        spheres.clear();
        
        // Criar esferas brancas nos quadrados pretos
        float sphereRadius = 18.0f; // Raio da esfera
        
        for (int checkerX = 0; checkerX < WIDTH / 50; checkerX++) {
            for (int checkerY = 0; checkerY < HEIGHT / 50; checkerY++) {
                // Verifica se é um quadrado preto
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
    ReSTIRRenderer() {
        scene.setupLights();
        scene.setupSpheres();
        srand(static_cast<unsigned int>(time(NULL)));
    }
    
    SurfacePoint createSurfacePoint(float x, float y) const {
        Vec3 rayOrigin(x - WIDTH/2, y - HEIGHT/2, 100); // Câmera
        Vec3 rayDir(0, 0, -1); // Olhando para baixo no eixo Z
        
        // Primeiro, testa interseção com esferas
        float closestDistance = 1e30f;
        int closestSphere = -1;
        
        for (int i = 0; i < static_cast<int>(scene.spheres.size()); i++) {
            float distance = scene.spheres[i].intersect(rayOrigin, rayDir);
            if (distance > 0 && distance < closestDistance) {
                closestDistance = distance;
                closestSphere = i;
            }
        }
        
        // Se intersectou com uma esfera, retorna ponto da esfera
        if (closestSphere >= 0) {
            Vec3 hitPoint = rayOrigin + rayDir * closestDistance;
            Vec3 normal = scene.spheres[closestSphere].getNormal(hitPoint);
            Color albedo = scene.spheres[closestSphere].albedo;
            
            return SurfacePoint(hitPoint, normal, albedo, true);
        }
        
        // Caso contrário, retorna ponto do plano xadrez
        Vec3 position(x - WIDTH/2, y - HEIGHT/2, 0);
        Vec3 normal(0, 0, 1);
        
        // Usar floor() para evitar problemas de truncamento
        int checkerX = static_cast<int>(floor(x / 50.0f));
        int checkerY = static_cast<int>(floor(y / 50.0f));
        int checker = (checkerX + checkerY) % 2;
        
        // Cores mais saturadas para o xadrez
        Color albedo = checker ? Color(0.9f, 0.9f, 0.9f) : Color(0.1f, 0.1f, 0.1f);
        
        return SurfacePoint(position, normal, albedo, false);
    }
    
    Color renderPixel(float x, float y) {
        SurfacePoint point = createSurfacePoint(x, y);
        
        // Fase de amostragem inicial
        Reservoir reservoir;
        
        // Gerar múltiplas amostras candidatas
        for (int i = 0; i < MAX_CANDIDATES; i++) {
            int lightIndex = randomInt(static_cast<int>(scene.lights.size()));
            reservoir.update(scene.lights, point, lightIndex);
        }
        
        // NOVA SEÇÃO: Reutilização espacial condicional
        if (ENABLE_SPATIAL_REUSE) {
            // Reutilização espacial com coordenadas mais consistentes
            for (int dx = -1; dx <= 1; dx += 2) {  // Só diagonais
                for (int dy = -1; dy <= 1; dy += 2) {
                    // Usar múltiplos exatos do tamanho do quadrado para evitar interferência
                    float nx = x + dx * 50;  // Mudança: usar 50 em vez de 20
                    float ny = y + dy * 50;
                    
                    if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
                        // Simular reservoir de pixel vizinho
                        Reservoir neighborReservoir;
                        int neighborLight = randomInt(static_cast<int>(scene.lights.size()));
                        SurfacePoint neighborPoint = createSurfacePoint(nx, ny);
                        
                        for (int j = 0; j < 8; j++) {  // Menos amostras para vizinhos
                            int lightIdx = randomInt(static_cast<int>(scene.lights.size()));
                            neighborReservoir.update(scene.lights, neighborPoint, lightIdx);
                        }
                        
                        reservoir.combine(neighborReservoir, scene.lights, point);
                    }
                }
            }
        }
        
        // Obter cor final do reservoir
        Color finalColor = reservoir.getFinalColor();
        
        // Iluminação ambiente mínima para destacar as luzes projetadas
        Color ambient = point.albedo * 0.01f;
        finalColor += ambient;
        
        return finalColor;
    }
    
    vector<Color> render() {
        vector<Color> image(WIDTH * HEIGHT);
        
        cout << "Renderizando cena " << WIDTH << "x" << HEIGHT << "..." << endl;
        cout << "Usando RIS BIASED com esferas brancas nos quadrados pretos" << endl;
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
    cout << "  -c, --candidates <número>      Define MAX_CANDIDATES (padrão: 30)" << endl;
    cout << "  -s, --spatial-reuse            Ativa amostragem espacial (padrão: ativada)" << endl;
    cout << "      --no-spatial-reuse         Desativa amostragem espacial" << endl;
    cout << "  -h, --help                     Mostra esta ajuda" << endl;
    cout << endl;
    cout << "Exemplos:" << endl;
    cout << "  " << programName << " -c 50                    # 50 candidatos com amostragem espacial" << endl;
    cout << "  " << programName << " --no-spatial-reuse       # Sem amostragem espacial" << endl;
    cout << "  " << programName << " -c 20 --no-spatial-reuse # 20 candidatos sem amostragem espacial" << endl;
    cout << "  " << programName << " --candidates 40 -s       # 40 candidatos com amostragem espacial" << endl;
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
    oss << "restir_spheres_" << MAX_CANDIDATES << "_";
    if (ENABLE_SPATIAL_REUSE) {
        oss << "spatial";
    } else {
        oss << "nospatial";
    }
    oss << "_biased.ppm";
    return oss.str();
}

int main(int argc, char* argv[]) {

    // 1. Configura a localidade para Português do Brasil
    // Isso afeta funções como printf, cout, etc.
    setlocale(LC_ALL, "Portuguese"); // Ou "pt_BR.UTF-8" se o seu console suportar UTF-8

    // 2. Configura a página de código do console para a codificação desejada
    // CP_UTF8 para UTF-8, 850 para DOS Latin 1 (comum no Brasil), 1252 para ANSI Latin 1
    // Experimente 850 primeiro, é o mais comum para o console no Windows.
    SetConsoleOutputCP(850); // Para codificação CP850 (DOS)
    // Ou SetConsoleOutputCP(CP_UTF8); // Se o seu console suportar UTF-8 e você salvou o arquivo como UTF-8
		
	
    cout << "=== Renderizador ReSTIR com Esferas Brancas nos Quadrados Pretos ===" << endl;
    
    // Parse dos argumentos de linha de comando
    if (!parseArguments(argc, argv)) {
        return 1;
    }
    
    cout << "Configuração:" << endl;
    cout << "  MAX_CANDIDATES: " << MAX_CANDIDATES << endl;
    cout << "  AMOSTRAGEM_ESPACIAL: " << (ENABLE_SPATIAL_REUSE ? "ATIVADA" : "DESATIVADA") << endl;
    cout << "  Algoritmo: RIS BIASED com esferas brancas lambertianas" << endl;
    cout << endl;
    
    ReSTIRRenderer renderer;
    vector<Color> image = renderer.render();
    
    string filename = generateFilename();
    renderer.saveImage(image, filename);
    
    cout << "Programa finalizado com sucesso!" << endl;
    cout << "Abra o arquivo '" << filename << "' para ver o resultado!" << endl;
    cout << "\nCOMPARAÇÃO RECOMENDADA:" << endl;
    cout << "Execute com e sem amostragem espacial para ver a diferença:" << endl;
    cout << "  " << argv[0] << " --spatial-reuse" << endl;
    cout << "  " << argv[0] << " --no-spatial-reuse" << endl;
    cout << "\nCARACTERÍSTICAS IMPLEMENTADAS:" << endl;
    cout << "- Esferas brancas com albedo lambertiano nos quadrados pretos" << endl;
    cout << "- Interseção raio-esfera para renderização 3D das esferas" << endl;
    cout << "- Controle via linha de comando da amostragem espacial" << endl;
    cout << "- Nomes de arquivo automáticos incluindo configuração" << endl;
    cout << "- Mesmas luzes nas posições e cores originais" << endl;
    
    return 0;
}
