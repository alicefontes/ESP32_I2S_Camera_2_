//camera com alteracoes
//recebe o XClk

#include "OV7670.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClient.h>
#include "BMP.h"

#include "driver/pcnt.h"

//novo
#include "driver/rmt.h"

const int SIOD = 21;
const int SIOC = 22;

const int VSYNC = 34;
const int HREF = 35;

const int XCLK = 32;
const int PCLK = 33;

const int D0 = 27;
const int D1 = 18;
const int D2 = 19;
const int D3 = 15;
const int D4 = 14;
const int D5 = 13;
const int D6 = 25;
const int D7 = 4;

const int SLAVE_READY = 26;
// const int TRIGGER = 26;
// const int TRIGGER_IN = 23;

const int TFT_DC = 2;
const int TFT_CS = 5;


#define ssid1        "Wonderland"
#define password1    "Fontes1995!"


Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, 0);
OV7670 *camera;

WiFiMulti wifiMulti;
WiFiServer server(80);

unsigned char bmpHeader[BMP::headerSize];

volatile bool captureRequested = false;

//novo
// =====================================================
// MARCADOR DE VSYNC PARA A MESTRE
// =====================================================

const int VSYNC_EVENT_OUT = 32;

rmt_obj_t* rmtVsyncOut = nullptr;

/*
 * =========================================================
 * MEDICAO PCLK
 * =========================================================
 *
 * O PCNT do ESP32 conta as bordas de subida do PCLK
 * diretamente em hardware.
 *
 * Isso permite observar a frequencia do PCLK sem
 * executar uma interrupcao a cada borda.
 *
 * NAO altera:
 * - configuracao da camera
 * - I2S
 * - DMA
 * =========================================================
 */

void medirPCLK()
{
  Serial.println();
  Serial.println("========== MEDICAO PCLK ==========");
  Serial.println("[PCLK] Contando bordas de subida durante 1 ms...");

  pcnt_config_t pcntConfig = {};

  pcntConfig.pulse_gpio_num = PCLK;
  pcntConfig.ctrl_gpio_num = PCNT_PIN_NOT_USED;

  pcntConfig.channel = PCNT_CHANNEL_0;
  pcntConfig.unit = PCNT_UNIT_0;

  pcntConfig.pos_mode = PCNT_COUNT_INC;
  pcntConfig.neg_mode = PCNT_COUNT_DIS;

  pcntConfig.lctrl_mode = PCNT_MODE_KEEP;
  pcntConfig.hctrl_mode = PCNT_MODE_KEEP;

  pcntConfig.counter_h_lim = 32767;
  pcntConfig.counter_l_lim = 0;

  pcnt_unit_config(&pcntConfig);

  pcnt_counter_pause(PCNT_UNIT_0);
  pcnt_counter_clear(PCNT_UNIT_0);

  pcnt_counter_resume(PCNT_UNIT_0);

  unsigned long inicio = micros();

  /*
   * Janela de medicao de aproximadamente 1 ms.
   *
   * Durante esse intervalo o PCNT conta as bordas
   * diretamente em hardware.
   */
  while ((micros() - inicio) < 1000)
  {
    // contador trabalhando em hardware
  }

  unsigned long fim = micros();

  pcnt_counter_pause(PCNT_UNIT_0);

  int16_t contador = 0;
  pcnt_get_counter_value(PCNT_UNIT_0, &contador);

  unsigned long duracao = fim - inicio;

  float frequencia =
      ((float)contador * 1000000.0f) /
      (float)duracao;

  Serial.printf(
    "[PCLK] Bordas contadas = %d\n",
    contador
  );

  Serial.printf(
    "[PCLK] Janela de medicao = %lu us\n",
    duracao
  );

  Serial.printf(
    "[PCLK] Frequencia aproximada = %.3f MHz\n",
    frequencia / 1000000.0f
  );

  Serial.println("[PCLK] Medicao concluida.");
  Serial.println("========== FIM PCLK ==========");
  Serial.println();

  pcnt_counter_clear(PCNT_UNIT_0);
  pcnt_counter_resume(PCNT_UNIT_0);
}

void testarVSYNCLocal()
{
  Serial.println();
  Serial.println("========== TESTE BRUTO VSYNC CAM 2 - ESCRAVA ==========");

  pinMode(VSYNC, INPUT);

  int estadoAnterior = digitalRead(VSYNC);

  unsigned long inicio = micros();
  unsigned long ultimaMudanca = inicio;

  int mudancas = 0;

  while ((micros() - inicio) < 1000000UL)
  {
    int estadoAtual = digitalRead(VSYNC);

    if (estadoAtual != estadoAnterior)
    {
      unsigned long agora = micros();

      Serial.print("[CAM 2 - GPIO34] ");
      Serial.print(estadoAnterior);
      Serial.print(" -> ");
      Serial.print(estadoAtual);
      Serial.print(" | dt = ");
      Serial.print(agora - ultimaMudanca);
      Serial.println(" us");

      ultimaMudanca = agora;
      estadoAnterior = estadoAtual;
      mudancas++;
    }
  }

  Serial.print("[CAM 2 - GPIO34] Mudancas em 1 segundo = ");
  Serial.println(mudancas);

  Serial.println("========== FIM TESTE ==========");
}

// void medirVSYNC()
// {
//   Serial.println();
//   Serial.println("========== MEDICAO VSYNC ==========");
//   Serial.println("[VSYNC] Medindo HIGH/LOW por 10 ciclos...");

//   unsigned long t0, t1;
//   unsigned long highTime, lowTime;
//   unsigned long period;

//   // Garante que começamos em LOW
//   while (digitalRead(VSYNC) == HIGH);

//   for (int i = 0; i < 10; i++)
//   {
//     // Espera subida
//     while (digitalRead(VSYNC) == LOW);
//     t0 = micros();

//     // Espera descida
//     while (digitalRead(VSYNC) == HIGH);
//     t1 = micros();

//     highTime = t1 - t0;

//     // Espera próxima subida
//     t0 = micros();
//     while (digitalRead(VSYNC) == LOW);
//     t1 = micros();

//     lowTime = t1 - t0;

//     period = highTime + lowTime;

//     Serial.print("[VSYNC] Ciclo ");
//     Serial.print(i + 1);
//     Serial.print(" | HIGH = ");
//     Serial.print(highTime);
//     Serial.print(" us | LOW = ");
//     Serial.print(lowTime);
//     Serial.print(" us | PERIODO = ");
//     Serial.print(period);
//     Serial.println(" us");
//   }

//   Serial.println("[VSYNC] Medicao concluida.");
//   Serial.println("========== FIM VSYNC ==========");
// }

// Função de medição de VSYNC com filtro de ruído elétrico (Debouncing de 10us)
// void medirVSYNC()
// {
//   Serial.println("\n========== MEDICAO VSYNC FILTRADA (ESCRAVA) ==========");
//   pinMode(VSYNC, INPUT);

//   for (int i = 0; i < 10; i++)
//   {
//     // 1. Espera subida REAL (HIGH sustentado por > 10us)
//     unsigned long tStartHigh = 0;
//     while (true)
//     {
//       while (digitalRead(VSYNC) == LOW) { delayMicroseconds(1); }
//       tStartHigh = micros();
//       delayMicroseconds(10); 
//       if (digitalRead(VSYNC) == HIGH) break; // Confirma borda real
//     }

//     // 2. Espera descida REAL (LOW sustentado por > 10us)
//     unsigned long tStartLow = 0;
//     while (true)
//     {
//       while (digitalRead(VSYNC) == HIGH) { delayMicroseconds(1); }
//       tStartLow = micros();
//       delayMicroseconds(10);
//       if (digitalRead(VSYNC) == LOW) break; // Confirma descida real
//     }

//     // 3. Espera próxima subida REAL
//     unsigned long tEnd = 0;
//     while (true)
//     {
//       while (digitalRead(VSYNC) == LOW) { delayMicroseconds(1); }
//       tEnd = micros();
//       delayMicroseconds(10);
//       if (digitalRead(VSYNC) == HIGH) break;
//     }

//     unsigned long highTime = tStartLow - tStartHigh;
//     unsigned long lowTime = tEnd - tStartLow;
//     unsigned long periodo = highTime + lowTime;

//     Serial.printf("[VSYNC ESCRAVA] Ciclo %d | HIGH = %lu us | LOW = %lu us | PERIODO = %lu us\n", 
//                   i + 1, highTime, lowTime, periodo);
//   }
//   Serial.println("========== FIM VSYNC ==========\n");
// }

//essa foi a menos pior
void medirVSYNC()
{
  Serial.println("\n========== MEDICAO VSYNC COM FILTRO ANTIRUIDO (ESCRAVA) ==========");
  pinMode(VSYNC, INPUT);

  for (int i = 0; i < 10; i++)
  {
    unsigned long tStartHigh = 0;
    unsigned long tStartLow = 0;
    unsigned long tEnd = 0;

    // 1. Espera subida REAL (Sinal precisa ficar HIGH por > 10us para ignorar glitch)
    while (true) {
      while (digitalRead(VSYNC) == LOW); // Espera subir
      tStartHigh = micros();
      delayMicroseconds(10);             // Confirmação antiruído
      if (digitalRead(VSYNC) == HIGH) break; // Se continuar HIGH, é sinal válido
    }

    // 2. Espera descida REAL (Sinal precisa ficar LOW por > 10us)
    while (true) {
      while (digitalRead(VSYNC) == HIGH); // Espera descer
      tStartLow = micros();
      delayMicroseconds(10);              // Confirmação antiruído
      if (digitalRead(VSYNC) == LOW) break;
    }

    // 3. Espera próxima subida REAL
    while (true) {
      while (digitalRead(VSYNC) == LOW);
      tEnd = micros();
      delayMicroseconds(10);
      if (digitalRead(VSYNC) == HIGH) break;
    }

    unsigned long highTime = tStartLow - tStartHigh;
    unsigned long lowTime = tEnd - tStartLow;
    unsigned long periodo = highTime + lowTime;

    Serial.printf("[VSYNC ESCRAVA] Ciclo %d | HIGH = %lu us | LOW = %lu us | PERIODO = %lu us\n", 
                  i + 1, highTime, lowTime, periodo);
                  
    delay(5); // Alimenta o Watchdog entre ciclos
  }
  Serial.println("========== FIM VSYNC ==========\n");
}

// void medirVSYNC() {
//   Serial.println("\n========== MEDICAO VSYNC FILTRADA ==========");
//   pinMode(VSYNC, INPUT);

//   for (int i = 0; i < 10; i++) {
//     // 1. Espera subida REAL (High > 50us para evitar ruído)
//     unsigned long tStartHigh = 0;
//     while (true) {
//       while (digitalRead(VSYNC) == LOW);
//       tStartHigh = micros();
//       delayMicroseconds(10); 
//       if (digitalRead(VSYNC) == HIGH) break; // Confirma que é HIGH real
//     }

//     // 2. Espera descida REAL
//     unsigned long tStartLow = 0;
//     while (true) {
//       while (digitalRead(VSYNC) == HIGH);
//       tStartLow = micros();
//       delayMicroseconds(10);
//       if (digitalRead(VSYNC) == LOW) break; // Confirma que é LOW real
//     }

//     // 3. Espera próxima subida
//     unsigned long tEnd = 0;
//     while (true) {
//       while (digitalRead(VSYNC) == LOW);
//       tEnd = micros();
//       delayMicroseconds(10);
//       if (digitalRead(VSYNC) == HIGH) break;
//     }

//     unsigned long highTime = tStartLow - tStartHigh;
//     unsigned long lowTime = tEnd - tStartLow;
//     unsigned long periodo = highTime + lowTime;

//     Serial.printf("[VSYNC] Ciclo %d | HIGH = %lu us | LOW = %lu us | PERIODO = %lu us\n", 
//                   i + 1, highTime, lowTime, periodo);
//   }
//   Serial.println("========== FIM VSYNC ==========");
// }

/*
 * =========================================================
 * SERVIDOR WEB
 * =========================================================
 */

void serve()
{
  WiFiClient client = server.available();

  if (client)
  {
    String currentLine = "";

    while (client.connected())
    {
      if (client.available())
      {
        char c = client.read();

        if (c == '\n')
        {
          if (currentLine.length() == 0)
          {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            client.print(
              "<style>"
              "body{margin:0}"
              "button{font-size:24px;padding:12px 24px;margin:10px}"
              "img{height:100%;width:auto}"
              "</style>"

              "<button onclick='fetch(\"/trigger\")'>CAPTURAR</button>"

              "<img id='a' src='/camera' "
              "onload='this.style.display=\"initial\"; "
              "var b=document.getElementById(\"b\"); "
              "b.style.display=\"none\"; "
              "b.src=\"camera?\"+Date.now();'>"

              "<img id='b' style='display:none' src='/camera' "
              "onload='this.style.display=\"initial\"; "
              "var a=document.getElementById(\"a\"); "
              "a.style.display=\"none\"; "
              "a.src=\"camera?\"+Date.now();'>"
            );

            client.println();
            break;
          }
          else
          {
            currentLine = "";
          }
        }
        else if (c != '\r')
        {
          currentLine += c;
        }

        // TRIGGER
        if (currentLine.endsWith("GET /trigger"))
        {
          triggerCapture();

          client.println("HTTP/1.1 200 OK");
          client.println("Content-type:text/plain");
          client.println();
          client.println("TRIGGER OK");
        }

        // CAMERA
        if (currentLine.endsWith("GET /camera"))
        {
          client.println("HTTP/1.1 200 OK");
          client.println("Content-type:image/bmp");
          client.println();

          client.write(bmpHeader, BMP::headerSize);

          client.write(
            camera->frame,
            camera->xres * camera->yres * 2
          );
        }
      }
    }

    client.stop();
  }
}


/*
 * =========================================================
 * TRIGGER
 * =========================================================
 */

// void triggerCapture()
// {
//   Serial.println("[TRIGGER] Disparo!");

//   digitalWrite(TRIGGER, HIGH);

//   Serial.println("[TRIGGER] Pulso iniciado.");

//   unsigned long inicio = millis();
//   bool triggerDetectado = false;

//   while (millis() - inicio < 20)
//   {
//     if (digitalRead(TRIGGER_IN) == HIGH)
//     {
//       triggerDetectado = true;
//       break;
//     }
//   }

//   digitalWrite(TRIGGER, LOW);

//   Serial.println("[TRIGGER] Pulso finalizado.");

//   if (triggerDetectado)
//   {
//     Serial.println("[TRIGGER] Sinal detectado no GPIO23!");

//     captureRequested = true;
//   }
//   else
//   {
//     Serial.println(
//       "[TRIGGER] ERRO: sinal nao detectado no GPIO23!"
//     );
//   }
// }


void triggerCapture()
{
  Serial.println("[TRIGGER] Disparo!");
  captureRequested = true;
}

// Função para aguardar a estabilização do XCLK Mestre na Câmera #2
// void aguardarXclkMestre(int pinoVSYNC) {
//   pinMode(pinoVSYNC, INPUT);
//   Serial.println("[ESCRAVO] Aguardando sinal do XCLK Mestre no sensor...");

//   unsigned long timeout = millis();
//   int pulsosDetectados = 0;

//   // Aguarda detectar pelo menos 5 alternâncias de nível no VSYNC
//   // para confirmar que o XCLK mestre está alimentando a câmera #2
//   while (pulsosDetectados < 5) {
//     if (digitalRead(pinoVSYNC) == HIGH) {
//       while (digitalRead(pinoVSYNC) == HIGH) {
//         // Aguarda a borda de descida
//         if (millis() - timeout > 10000) break; // Timeout de segurança de 10s
//       }
//       pulsosDetectados++;
//     }

//     // Caso o Mestre ainda não tenha ligado, reinicia o timeout para não travar
//     if (millis() - timeout > 10000) {
//       Serial.println("[ESCRAVO] Aguardando o ESP32 Mestre ligar...");
//       timeout = millis();
//     }
//     yield(); // Alimenta o Watchdog do ESP32
//   }

//   Serial.println("[ESCRAVO] XCLK Mestre detectado e sincronizado!");
// }

/*
 * =========================================================
 * SETUP
 * =========================================================
 */

// void setup()
// {
//   Serial.begin(115200);

//   // =====================================================
//   // TRIGGER
//   // =====================================================

//   // pinMode(TRIGGER, OUTPUT);
//   // digitalWrite(TRIGGER, LOW);

//   // pinMode(TRIGGER_IN, INPUT);

//   // Serial.println("[TRIGGER] GPIO26 = OUTPUT");
//   // Serial.println("[TRIGGER] GPIO23 = INPUT");

//   // =====================================================
//   // SETUP ORIGINAL
//   // =====================================================
//   pinMode(SLAVE_READY, OUTPUT);
//   digitalWrite(SLAVE_READY, LOW);

//   pinMode(VSYNC, INPUT);

//   Serial.println("[SETUP] Inicio");

//   wifiMulti.addAP(ssid1, password1);

//   Serial.println("[WIFI] Conectando...");

//   if (wifiMulti.run() == WL_CONNECTED)
//   {
//     Serial.println("");
//     Serial.println("[WIFI] WiFi connected");
//     Serial.println("[WIFI] IP address: ");
//     Serial.println(WiFi.localIP());
//   }
//   Serial.println("[WIFI] Saiu da conexão.");

//   // Serial.println("[CAMERA #1] Antes de criar OV7670");

//   // 2. BLOQUEIO DE SEGURANÇA:
//   // Só avança para a Câmera quando o Mestre injetar os 10MHz de XCLK
//   // aguardarXclkMestre(VSYNC);

//   // // Pequeña pausa de 100ms para a câmera #2 responder limpo ao I2C
//   // delay(100);

//   Serial.println("[ESCRAVO] ESP32 inicializada.");
//   Serial.println("[ESCRAVO] Enviando READY para a Mestre...");

//   digitalWrite(SLAVE_READY, HIGH);

//   Serial.println("[ESCRAVO] READY = HIGH.");
//   Serial.println("[ESCRAVO] Aguardando XCLK da Mestre...");

//   delay(200);

//   Serial.println("[ESCRAVO] Iniciando câmera #2...");

//   camera = new OV7670(
//       OV7670::Mode::QQVGA_RGB565,
//       SIOD, SIOC, VSYNC, HREF, XCLK, PCLK,
//       D0, D1, D2, D3, D4, D5, D6, D7
//   );

//   Serial.println("[CAMERA] OV7670 criada");

//   Serial.println("[BMP] Antes");

//   BMP::construct16BitHeader(
//       bmpHeader,
//       camera->xres,
//       camera->yres
//   );

//   Serial.println("[BMP] OK");

//   Serial.println("[TFT] Antes");

//   tft.initR(INITR_BLACKTAB);
//   tft.fillScreen(0);

//   Serial.println("[TFT] OK");

//   Serial.println("[SERVER] Antes");

//   server.begin();

//   Serial.println("[SERVER] OK");

//   Serial.println("[TRIGGER] Sistema pronto.");
//   // testarVSYNCLocal();

//   medirVSYNC();


//   // =====================================================
//   // MEDICAO PCLK
//   // =====================================================
//   //
//   // Somente observacao do PCLK.
//   // Nenhuma configuracao da camera, I2S ou DMA e alterada.
//   //

//   //medirPCLK();
// }

//esse tvaa funcionando bem
void setup()
{
  Serial.begin(115200);

  // Garante pino de aviso em LOW durante a inicialização
  pinMode(SLAVE_READY, OUTPUT);
  digitalWrite(SLAVE_READY, LOW);

  wifiMulti.addAP(ssid1, password1);
  Serial.println("[WIFI] Conectando...");

  if (wifiMulti.run() == WL_CONNECTED)
  {
    Serial.println("[WIFI] Conectado!");
  }

  // Avisa a Mestre que o ESP32 escravo terminou o boot e o Wi-Fi
  digitalWrite(SLAVE_READY, HIGH);
  Serial.println("[ESCRAVO] READY enviado (HIGH). Aguardando XCLK da Mestre...");

  // Dá um pequeno tempo para a Mestre estabilizar o XCLK
  delay(100);

  // Inicializa a câmera com o XCLK já fornecido pela Mestre
  camera = new OV7670(
      OV7670::Mode::QQVGA_RGB565,
      SIOD, SIOC, VSYNC, HREF, XCLK, PCLK,
      D0, D1, D2, D3, D4, D5, D6, D7
  );

  BMP::construct16BitHeader(bmpHeader, camera->xres, camera->yres);
  tft.initR(INITR_BLACKTAB);
  tft.fillScreen(0);
  server.begin();

  // =====================================================
  // RMT TX - MARCADOR DE VSYNC
  // GPIO32 da escrava -> GPIO23 da mestre
  // =====================================================

  pinMode(VSYNC_EVENT_OUT, OUTPUT);
  digitalWrite(VSYNC_EVENT_OUT, LOW);

  rmtVsyncOut = rmtInit(
      VSYNC_EVENT_OUT,
      true,          // TX
      RMT_MEM_64
  );

  if (rmtVsyncOut == nullptr)
  {
    Serial.println("[RMT] ERRO ao inicializar TX no GPIO32.");
  }
  else
  {
    // 100 ns por tick = 10 MHz
    float tickReal = rmtSetTick(rmtVsyncOut, 100.0);

    Serial.print("[RMT] TX GPIO32 OK. Tick = ");
    Serial.print(tickReal);
    Serial.println(" ns");
  }

  // medirVSYNC();
}


// void setup()
// {
//   Serial.begin(115200);

//   // Garante pino READY em LOW durante o boot inicial
//   pinMode(SLAVE_READY, OUTPUT);
//   digitalWrite(SLAVE_READY, LOW);

//   wifiMulti.addAP(ssid1, password1);
//   Serial.println("[WIFI] Conectando...");

//   if (wifiMulti.run() == WL_CONNECTED)
//   {
//     Serial.println("[WIFI] Conectado!");
//     Serial.print("[WIFI] IP: ");
//     Serial.println(WiFi.localIP());
//   }

//   // Envia sinal READY para a Mestra ativar o XCLK
//   digitalWrite(SLAVE_READY, HIGH);
//   Serial.println("[ESCRAVO] READY enviado (HIGH). Aguardando XCLK da Mestre...");

//   // Dá 150 ms para a Mestra receber o READY e habilitar a saída do gerador de clock
//   delay(150);

//   // Inicializa o driver da câmera com o XCLK fornecido
//   camera = new OV7670(
//       OV7670::Mode::QQVGA_RGB565,
//       SIOD, SIOC, VSYNC, HREF, XCLK, PCLK,
//       D0, D1, D2, D3, D4, D5, D6, D7
//   );

//   BMP::construct16BitHeader(bmpHeader, camera->xres, camera->yres);
//   tft.initR(INITR_BLACKTAB);
//   tft.fillScreen(0);
//   server.begin();

//   // Executa a medição de VSYNC agora que a câmera e o Wi-Fi estão estáveis
//   medirVSYNC();
// }
/*
 * =========================================================
 * DISPLAY
 * =========================================================
 */

void displayRGB565(
  unsigned char * frame,
  int xres,
  int yres
)
{
  tft.setAddrWindow(0, 0, yres - 1, xres - 1);

  int i = 0;

  for (int x = 0; x < xres; x++)
    for (int y = 0; y < yres; y++)
    {
      i = (y * xres + x) << 1;

      tft.pushColor(
        frame[i] |
        (frame[i + 1] << 8)
      );
    }
}



/*
 * =========================================================
 * LOOP
 * =========================================================
 */

void loop()
{
  //novo
  if (rmtVsyncOut != nullptr)
  {
    // Detecta a borda de VSYNC através do estado do GPIO34.
    static int estadoVSYNCAnterior = LOW;

    int estadoVSYNCAtual = digitalRead(VSYNC);

    if (estadoVSYNCAtual == HIGH && estadoVSYNCAnterior == LOW)
    {
      rmt_data_t pulso;

      pulso.duration0 = 10;  // 1 us
      pulso.level0 = 1;

      pulso.duration1 = 10;  // 1 us
      pulso.level1 = 0;

      rmtWrite(rmtVsyncOut, &pulso, 1);

    }

    estadoVSYNCAnterior = estadoVSYNCAtual;
  }

  serve();

  if (captureRequested)
  {
    captureRequested = false;

    Serial.println("[CAPTURE] Trigger recebido.");

    Serial.println(
      "[CAPTURE] Capturando proximo frame..."
    );

    camera->oneFrame();

    Serial.println(
      "[CAPTURE] Frame capturado."
    );

    displayRGB565(
      camera->frame,
      camera->xres,
      camera->yres
    );
  }
}