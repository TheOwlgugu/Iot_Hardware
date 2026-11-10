#include "Serial3.h"
#include <string.h>
#if SERIAL3_MIRROR_OLED
#include "OLED.h"
#endif

/* ==================== 接收环形缓冲区 ==================== */
#define SERIAL3_RX_BUFFER_SIZE  128
//（PB10 = TX，PB11 = RX，波特率 115200）
static uint8_t          rx_buffer[SERIAL3_RX_BUFFER_SIZE];
static volatile uint16_t rx_head = 0;   // 中断写入位置
static volatile uint16_t rx_tail = 0;   // 主循环读取位置
#if SERIAL3_MIRROR_OLED

#define MIRROR_ROWS  4    // OLED 行数
#define MIRROR_COLS  16   // OLED 每行字符数

static char    mirror_buf[MIRROR_ROWS][MIRROR_COLS + 1];   // 屏幕内容的备份，每行以 '\0' 结尾
static uint8_t mirror_row = 0;                             // 当前光标行 0~3
static uint8_t mirror_col = 0;                             // 当前光标列 0~15
static uint8_t mirror_ready = 0;

/**
 * @brief  用备份内容整屏重绘
  */
static void Mirror_Redraw(void)
{
    for (uint8_t r = 0; r < MIRROR_ROWS; r++)
        OLED_ShowString(r + 1, 1, mirror_buf[r]);
}

/**
 * @brief  换行：没到最后一行就下移，到了最后一行就整体上滚一行
  */
static void Mirror_NewLine(void)
{
    mirror_col = 0;
    if (mirror_row < MIRROR_ROWS - 1)
    {
        mirror_row++;
        return;
    }
    for (uint8_t r = 0; r < MIRROR_ROWS - 1; r++)
        memcpy(mirror_buf[r], mirror_buf[r + 1], MIRROR_COLS + 1);
    memset(mirror_buf[MIRROR_ROWS - 1], ' ', MIRROR_COLS);
    Mirror_Redraw();
}

/**
  * @brief  把串口发出的一个字节同步显示到 OLED（终端式：自动换行、满屏滚动）
  */
static void Mirror_Putc(uint8_t c)
{
    if (!mirror_ready)
    {
        for (uint8_t r = 0; r < MIRROR_ROWS; r++)
        {
            memset(mirror_buf[r], ' ', MIRROR_COLS);
            mirror_buf[r][MIRROR_COLS] = '\0';
        }
        mirror_ready = 1;
    }

    if (c == '\r') return;
    if (c == '\n')
    {
        if (mirror_col != 0) Mirror_NewLine();   // 行首的换行不处理，避免屏幕上出现空行
        return;
    }
    if (c < ' ' || c > '~') c = '?';             // 字库只有可见 ASCII，其余字节用 ? 代替
    if (mirror_col >= MIRROR_COLS) Mirror_NewLine();

    mirror_buf[mirror_row][mirror_col] = (char)c;
    OLED_ShowChar(mirror_row + 1, mirror_col + 1, (char)c);
    mirror_col++;
}

#endif

/**
 * @brief  USART3 初始化（PB10 = TX，PB11 = RX，波特率 115200）
 * @param  无
 * @retval 无
  */
void Serial3_Init(void)
{
    /* 开启时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);   // USART3 挂在 APB1 上
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);    // PB10/PB11 在 GPIOB

    /* GPIO 初始化 */
    GPIO_InitTypeDef GPIO_InitStructure;

    // PB10 → USART3_TX（复用推挽输出）
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // PB11 → USART3_RX（上拉输入）
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* USART3 初始化 */
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_Init(USART3, &USART_InitStructure);

    /* 开启接收中断 */
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

    /* NVIC 中断配置 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;   // 比 USART1(1) 低
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&NVIC_InitStructure);

    /* 使能 USART3 */
    USART_Cmd(USART3, ENABLE);
}

#if SERIAL3_MIRROR_OLED
static uint8_t mirror_on = 1;

void Serial3_MirrorEnable(uint8_t on)
{
    mirror_on = on;
}

uint8_t Serial3_MirrorEnabled(void)
{
    return mirror_on;
}
#endif

void Serial3_SendByte(uint8_t Byte)
{
    USART_SendData(USART3, Byte);
    while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
#if SERIAL3_MIRROR_OLED
    if (mirror_on) Mirror_Putc(Byte);   // 字节在串口移位发送的同时，顺便刷到 OLED 上
#endif
}

/**
 * @brief  发送一个数组
  */
void Serial3_SendArray(uint8_t *Array, uint16_t Length)
{
    for (uint16_t i = 0; i < Length; i++)
        Serial3_SendByte(Array[i]);
}

/**
  * @brief  发送一个字符串
  */
void Serial3_SendString(char *String)
{
    for (uint16_t i = 0; String[i] != '\0'; i++)
        Serial3_SendByte(String[i]);
}

/**
 * @brief  格式化打印（类似 printf）
  */
void Serial3_Printf(char *format, ...)
{
    char String[128];
    va_list arg;
    va_start(arg, format);
    vsnprintf(String, sizeof(String), format, arg);
    va_end(arg);
    Serial3_SendString(String);
}

/**
  * @brief  检查接收缓冲区是否有数据
  * @retval 1=有数据，0=空
  */
uint8_t Serial3_Available(void)
{
    return (rx_head != rx_tail);
}

/**
  * @brief  从接收缓冲区取一个字节
  * @retval 取出的字节；缓冲区空时返回 0
  */
uint8_t Serial3_GetByte(void)
{
    uint8_t data;

    if (rx_head == rx_tail) return 0;
    data = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % SERIAL3_RX_BUFFER_SIZE;
    return data;
}

/**
  * @brief  USART3 中断服务函数
  * @note   收到数据时写入环形缓冲区，主循环用 Serial3_Available/GetByte 读取
  */
void USART3_IRQHandler(void)
{
    if (USART_GetITStatus(USART3, USART_IT_RXNE) == SET)
    {
        uint8_t data = USART_ReceiveData(USART3);
        uint16_t next = (rx_head + 1) % SERIAL3_RX_BUFFER_SIZE;

        if (next != rx_tail)   /* 缓冲区未满，满了就丢弃 */
        {
            rx_buffer[rx_head] = data;
            rx_head = next;
        }

        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
    }

    /* 溢出错误处理，防止中断卡死 */
    if (USART_GetFlagStatus(USART3, USART_FLAG_ORE) == SET)
    {
        USART_ReceiveData(USART3);
        USART_ClearFlag(USART3, USART_FLAG_ORE);
    }
}
