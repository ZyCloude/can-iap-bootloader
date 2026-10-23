#include "main.h"
#include "OLED.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>

/**
  * 数据存储格式：
  * 纵向8点，高位在下，先从左到右，再从上到下
  * 每一个Bit对应一个像素点
  *
  *      B0 B0                  B0 B0
  *      B1 B1                  B1 B1
  *      B2 B2                  B2 B2
  *      B3 B3  ------------->  B3 B3 --
  *      B4 B4                  B4 B4  |
  *      B5 B5                  B5 B5  |
  *      B6 B6                  B6 B6  |
  *      B7 B7                  B7 B7  |
  *                                    |
  *  -----------------------------------
  *  |
  *  |   B0 B0                  B0 B0
  *  |   B1 B1                  B1 B1
  *  |   B2 B2                  B2 B2
  *  --> B3 B3  ------------->  B3 B3
  *      B4 B4                  B4 B4
  *      B5 B5                  B5 B5
  *      B6 B6                  B6 B6
  *      B7 B7                  B7 B7
  *
  * 坐标轴定义：
  * 左上角为(0, 0)点
  * 横向向右为X轴，取值范围：0~127
  * 纵向向下为Y轴，取值范围：0~63
  *
  *       0             X轴           127
  *      .------------------------------->
  *    0 |
  *      |
  *      |
  *      |
  *  Y轴 |
  *      |
  *      |
  *      |
  *   63 |
  *      v
  *
  */


/*全局变量*********************/

/**
  * OLED显存数组
  * 所有的显示函数，都只是对此显存数组进行读写
  * 随后调用OLED_Update函数或OLED_UpdateArea函数
  * 才会将显存数组的数据发送到OLED硬件，进行显示
  */
uint8_t OLED_DisplayBuf[8][128];

/*********************全局变量*/


/*引脚配置*********************/

/*引脚配置*/
/* pins -> main.h: OLED_SCL_GPIO_Port/OLED_SCL_Pin ; OLED_SDA_GPIO_Port/OLED_SDA_Pin */

/**
  * @brief  OLED写SCL函数
  * @param  要写入SCL的电平值，范围：0/1
  * @retval 无
  */
void OLED_Delay(void)
{
	volatile uint32_t i;
	for(i = 0; i < 100; i++);
}

void OLED_W_SCL(uint8_t BitValue)
{
	HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, (BitValue ? GPIO_PIN_SET : GPIO_PIN_RESET));
	OLED_Delay();
}

/**
  * @brief  OLED写SDA函数
  * @param  要写入SDA的电平值，范围：0/1
  * @retval 无
  */
void OLED_W_SDA(uint8_t BitValue)
{
	HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, (BitValue ? GPIO_PIN_SET : GPIO_PIN_RESET));
}

/**
  * @brief  I2C初始化函数，初始化PB8为I2C_SCL输出，PB9为I2C_SDA输出
  * @param  无
  * @retval 无
  */
void OLED_I2C_Init(void)
{
	uint32_t i,j;

	//初始化前，加入矢量延时，待OLED供电稳定
	for(i = 0; i < 1000; i++)
	{
		for(j = 0; j < 1000; j++);
	}

	/* GPIO configured by CubeMX MX_GPIO_Init (open-drain, pull-up) */
    OLED_W_SCL(1);
    OLED_W_SDA(1);}

/*********************引脚配置*/


/*通信协议*********************/

/**
  * @brief  I2C开始函数
  * @param  无
  * @retval 无
  */
void OLED_I2C_Start(void)
{
	OLED_W_SDA(1);
	OLED_W_SCL(1);
	OLED_W_SDA(0);
	OLED_W_SCL(0);
}

/**
  * @brief  I2C停止函数
  * @param  无
  * @retval 无
  */
void OLED_I2C_Stop(void)
{
	OLED_W_SDA(0);
	OLED_W_SCL(1);
	OLED_W_SDA(1);
}

/**
  * @brief  I2C发送一个字节函数
  * @param  Byte 要发送的一个字节
  * @retval 无
  */
void OLED_I2C_SendByte(uint8_t Byte)
{
	uint8_t i;

	for(i = 0; i < 8; i++)
	{
		OLED_W_SDA(Byte & (0x80 >> i));
		OLED_W_SCL(1);
		OLED_W_SCL(0);
	}
	OLED_W_SCL(1);    //额外的一个时钟，不处理应答信号
	OLED_W_SCL(0);
}

/**
  * @brief  OLED写命令函数
  * @param  Command 要写入的命令
  * @retval 无
  */
void OLED_WriteCommand(uint8_t Command)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);    //从机地址
	OLED_I2C_SendByte(0x00);    //写命令
	OLED_I2C_SendByte(Command);
	OLED_I2C_Stop();
}

/**
  * @brief  OLED写数据函数
  * @param  Data 要写入数据的起始地址
  * @param  Count 要写入数据的数量
  * @retval 无
  */
void OLED_WriteData(uint8_t *Data, uint8_t Count)
{
	uint8_t i;

	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);    //从机地址
	OLED_I2C_SendByte(0x40);    //写数据
	for(i = 0; i < Count; i++)
	{
		OLED_I2C_SendByte(Data[i]);
	}
	OLED_I2C_Stop();
}

/*通信协议*********************/


/*硬件配置*********************/

/**
  * @brief  OLED初始化函数
  * @param  无
  * @retval 无
  */
void OLED_Init(void)
{
	OLED_I2C_Init();            //端口初始化

	OLED_WriteCommand(0xAE);	//设置显示开启/关闭，0xAE关闭，0xAF开启

	OLED_WriteCommand(0xD5);    //设置显示时钟分频比/振荡器频率
	OLED_WriteCommand(0x80);    //0x00 ~ 0xFF

	OLED_WriteCommand(0xA8);    //设置多路复用率
	OLED_WriteCommand(0x3F);    //0x0E ~ 0x3F

	OLED_WriteCommand(0xD3);    //设置显示偏移
	OLED_WriteCommand(0x00);    //0x00 ~ 0x7F

	OLED_WriteCommand(0x40);    //设置显示开始行，0x40 ~ 0x7F

	OLED_WriteCommand(0xA1);    //设置左右方向，0xA1正常，0xA0左右反置

	OLED_WriteCommand(0xC8);    //设置上下方向，0xC8正常，0xC0上下反置

	OLED_WriteCommand(0xDA);    //设置COM引脚硬件配置
	OLED_WriteCommand(0x12);

	OLED_WriteCommand(0x81);    //设置对比度控制
	OLED_WriteCommand(0xCF);    //0x00 ~ 0xFF

	OLED_WriteCommand(0xD9);    //设置预充电周期
	OLED_WriteCommand(0xF1);

	OLED_WriteCommand(0xDB);    //设置VCOMH取消选择级别
	OLED_WriteCommand(0x30);

	OLED_WriteCommand(0xA4);    //设置整个显示打开/关闭

	OLED_WriteCommand(0xA6);    //设置正常/反色显示，0xA6正常，0xA7反色

	OLED_WriteCommand(0x8D);    //设置充电泵
	OLED_WriteCommand(0x14);

	OLED_WriteCommand(0xAF);    //开启显示

	OLED_Clear();               //清空显存数组
	OLED_Update();              //更新显示，清屏，防止初始化后未显示内容时花屏
}

/**
  * @brief  OLED设置光标位置函数
  * @param  Page 指定光标所在页，范围：0 ~ 7
  * @param  X 指定光标所在的X轴坐标，范围：0 ~ 127
  * @retval 无
  */
void OLED_SetCursor(uint8_t Page, uint8_t X)
{
	OLED_WriteCommand(0xB0 | Page);					//设置Y位置
	OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));	//设置X位置高4位
	OLED_WriteCommand(0x00 | (X & 0x0F));			//设置X位置低4位
}

/*********************硬件配置*/


/*工具函数*********************/

/*工具函数仅供内部部分函数使用*/

/**
  * @brief  OLED次方函数
  * @param  X 底数
  * @param  Y 指数
  * @retval 等于X的Y次方
  */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y--)
	{
		Result *= X;
	}
	return Result;
}

/**
  * @brief  判断指定点是否在指定多边形内部，此算法由W. Randolph Franklin提出
  * @param  nvert 多边形的顶点数
  * @param  vertx verty 包含多边形顶点的x和y坐标的数组
  * @param  testx testy 测试点的X和y坐标
  * @retval 指定点是否在指定多边形内部，1在内部，0不在内部
  */
uint8_t OLED_pnpoly(uint8_t nvert, int16_t *vertx, int16_t *verty, int16_t testx, int16_t testy)
{
	int16_t i, j, c = 0;

	for(i = 0, j = nvert - 1; i < nvert; j = i++)
	{
		if(((verty[i] > testy) != (verty[j] > testy)) &&
			(testx < (vertx[j] - vertx[i]) * (testy - verty[i]) / (verty[j] - verty[i]) + vertx[i]))
		{
			c = !c;
		}
	}
	return c;
}

/**
  * @brief  判断指定点是否在指定角度内部
  * @param  X Y 指定点的坐标
  * @param  StartAngle EndAngle 起始角度和终止角度，范围：-180 ~ 180
  *         水平向右为0度，水平向左为180度或-180度，下方为正数，上方为负数，顺时针旋转
  * @retval 指定点是否在指定角度内部，1在内部，0不在内部，默认原点在角度内部
  */
uint8_t OLED_IsInAngle(int16_t X, int16_t Y, int16_t StartAngle, int16_t EndAngle)
{
	int16_t PointAngle;

	PointAngle = atan2(Y, X) / 3.14 * 180;    //计算指定点的弧度，并转化为角度表示
	if(StartAngle > EndAngle)    //确保StartAngle小于等于EndAngle
	{
		StartAngle = -StartAngle;
		EndAngle = -EndAngle;
		PointAngle = -PointAngle;
	}
	if((PointAngle >= StartAngle && PointAngle <= EndAngle) || (X == 0 && Y == 0))  {return 1;}
	return 0;
}

/*********************工具函数*/


/*功能函数*********************/

/**
  * @brief  OLED更新函数，将OLED显存数组更新到OLED屏幕
  * @param  无
  * @retval 无
  */
void OLED_Update(void)
{
	uint8_t j;

	for(j = 0; j < 8; j++)
	{
		OLED_SetCursor(j, 0);
		OLED_WriteData(OLED_DisplayBuf[j], 128);
	}
}

/**
  * @brief  OLED区域更新函数，将OLED显存数组部分更新到OLED屏幕，
  *         此函数会至少更新参数指定的区域，如果更新区域Y轴只包含部分页，则同一页的剩余部分会跟随一起更新
  * @param  X 指定区域左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定区域左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Width 指定区域的宽度，范围：0 ~ 128
  * @param  Height 指定区域的高度，范围：0 ~ 64
  * @retval 无
  */
void OLED_UpdateArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
	int16_t j;
	int16_t Page, Page1;

	Page = Y / 8;
	Page1 = (Y + Height - 1) / 8 + 1;
	if(Y < 0)    //负数坐标在计算页地址时需要加一个偏移
	{
		Page -= 1;
		Page1 -= 1;
	}

	for(j = Page; j < Page1; j++)    //遍历指定区域涉及的相关页
	{
		if(X >= 0 && X <= 127 && j >= 0 && j <= 7)    //超出屏幕的内容不显示
		{
			OLED_SetCursor(j, X);
			OLED_WriteData(&OLED_DisplayBuf[j][X], Width);
		}
	}
}

/**
  * @brief  OLED清除函数，将OLED显存数组全部清零，
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  无
  * @retval 无
  */
void OLED_Clear(void)
{
	uint8_t i, j;

	for (j = 0; j < 8; j++)    //遍历指定页
	{
		for(i = 0; i < 128; i++)    //遍历指定列
		{
			OLED_DisplayBuf[j][i] = 0x00;
		}
	}
}

/**
  * @brief   OLED区域清除函数，将OLED显存数组部分清零，
  *          调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定区域左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定区域左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Width 指定区域的宽度，范围：0 ~ 128
  * @param  Height 指定区域的高度，范围：0 ~ 64
  * @retval 无
  */
void OLED_ClearArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
	int16_t i, j;

	for(j = Y; j < Y + Height; j++)    //遍历指定页
	{
		for(i = X; i < X + Width; i++)    //遍历指定列
		{
			if(i >= 0 && i <= 127 && j >= 0 && j <= 63)    //超出屏幕内容不显示
			{
				OLED_DisplayBuf[j / 8][i] &= ~(0x01 << (j % 8));
			}
		}
	}
}

/**
  * @brief  OLED反色函数，将OLED显存数组全部取反
            调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  无
  * @retval 无
  */
void OLED_Reverse(void)
{
	uint8_t i, j;

	for(j = 0; j < 8; j++)    //遍历指定页
	{
		for(i = 0; i < 128; i++)    //遍历指定列
		{
			OLED_DisplayBuf[j][i] ^= 0xFF;
		}
	}
}

/**
  * @brief   OLED区域反色函数，将OLED显存数组部分清零，
             调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定区域左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定区域左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Width 指定区域的宽度，范围：0 ~ 128
  * @param  Height 指定区域的高度，范围：0 ~ 64
  * @retval 无
  */
void OLED_ReverseArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height)
{
	int16_t i, j;

	for(j = Y; j < Y + Height; j++)    //遍历指定页
	{
		for(i = X; i < X + Width; i++)    //遍历指定列
		{
			if(i >= 0 && i <= 127 && j >= 0 && j <= 63)    //超出屏幕内容不显示
			{
				OLED_DisplayBuf[j / 8][i] ^= 0x01 << (j % 8);
			}
		}
	}
}

/**
  * @brief  OLED显示一个字符函数
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定字符左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定字符左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Char 指定要显示的字符，范围：ASCII可见字符
  * @param  FontSize 指定字体大小
  *         范围：OLED_8X16		宽8像素，高16像素
  *               OLED_6X8		宽6像素，高8像素
  * @retval 无
  */
void OLED_ShowChar(int16_t X, int16_t Y, char Char, uint8_t FontSize)
{
	if(FontSize == OLED_8X16)		//字体为宽8像素，高16像素
	{
		OLED_ShowImage(X, Y, 8, 16, OLED_F8x16[Char - ' ']);
	}
	else if(FontSize == OLED_6X8)	//字体为宽6像素，高8像素
	{
		OLED_ShowImage(X, Y, 6, 8, OLED_F6x8[Char - ' ']);
	}
}

/**
  * @brief  OLED显示字符串函数（支持ASCII码和中文混合写入，中文字符编码为GB2312）
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  *         显示的中文字符需要在OLED_Data.c里的OLED_CF16x16数组定义
  *         未找到指定中文字符时，会显示默认图形（一个方框，内部一个问号）
  *         当字体大小为OLED_8X16时，中文字符以16*16点阵正常显示
  *         当字体大小为OLED_6X8时，中文字符以6*8点阵显示'?'
  * @param  X 指定字符串左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定字符串左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  String 指定要显示的字符串，范围：ASCII码可见字符或GB2312中文字符组成的字符串
  * @param  FontSize 指定字体大小
  *         范围：OLED_8X16		宽8像素，高16像素
  *               OLED_6X8		宽6像素，高8像素
  * @retval 无
  */
void OLED_ShowString(int16_t X, int16_t Y, char *String, uint8_t FontSize)
{
	uint16_t i = 0;
	char SingleChar[3];
	uint8_t CharLength = 0;
	uint16_t XOffset = 0;
	uint16_t pIndex;

	while(String[i] != '\0')	//遍历字符串
	{
		//此段代码的目的是，提取GB2312字符串中的一个字符，转存到SingleChar子字符串中
		//判断GB2312字节的最高位标志位
		if((String[i] & 0x80) == 0x00)    //最高位为0
		{
			CharLength = 1;                 //字符为1字节
			SingleChar[0] = String[i++];    //将第一个字节写入SingleChar第0个位置，随后i指向下一个字节
			SingleChar[1] = '\0';           //为SingleChar添加字符串结束标志位
		}
		else    //最高位为1
		{
			CharLength = 2;                    //字符为2字节
			SingleChar[0] = String[i++];       //将第一个字节写入SingleChar第0个位置，随后i指向下一个字节
			if(String[i] == '\0')  {break;}    //意外情况，跳出循环，结束显示
			SingleChar[1] = String[i++];       //将第二个字节写入SingleChar第1个位置，随后i指向下一个字节
			SingleChar[2] = '\0';              //为SingleChar添加字符串结束标志位
		}

		//显示上述代码提取到的SingleChar
		if(CharLength == 1)    //如果是单字节字符
		{
			OLED_ShowChar(X + XOffset, Y, SingleChar[0], FontSize);
			XOffset += FontSize;
		}
		else    //否则，即多字节字符
		{
			//遍历整个字模库，从字模库中寻找此字符的数据
			//如果找到最后一个字符（定义为空字符串），则表示字符未在字模库定义，停止寻找
			for(pIndex = 0; strcmp(OLED_CF16x16[pIndex].Index, "") != 0; pIndex++)
			{
				//找到匹配的字符，则跳出循环，此时pIndex的值为指定字符的索引
				if(strcmp(OLED_CF16x16[pIndex].Index, SingleChar) == 0)  {break;}
			}
			if(FontSize == OLED_8X16)    //给定字体为8*16点阵
			{
				OLED_ShowImage(X + XOffset, Y, 16, 16, OLED_CF16x16[pIndex].Data);
				XOffset += 16;
			}
			else if(FontSize == OLED_6X8)    //给定字体为6*8点阵
			{
				//空间不足，此位置显示'?'
				OLED_ShowChar(X + XOffset, Y, '?', OLED_6X8);
				XOffset += OLED_6X8;
			}
		}
	}
}

/**
  * @brief  OLED显示数字函数（十进制，正整数）
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定数字左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定数字左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Number 指定要显示的数字，范围：0 ~ 4294967295
  * @param  Length 指定数字的长度，范围：1 ~ 10
  * @param  FontSize 指定字体大小
  *         范围：OLED_8X16		宽8像素，高16像素
  *               OLED_6X8		宽6像素，高8像素
  * @retval 无
  */
void OLED_ShowNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i;

	for(i = 0; i < Length; i++)    //遍历每一位数字
	{
		OLED_ShowChar(X + i * FontSize, Y, Number / OLED_Pow(10, Length - i - 1) % 10 + '0', FontSize);
	}
}

/**
  * @brief  OLED显示数字函数（十进制，整数）
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定数字左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定数字左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Number 指定要显示的数字，范围：-2147483648 ~ 2147483647
  * @param  Length 指定数字的长度，范围：1 ~ 10
  * @param  FontSize 指定字体大小
  *         范围：OLED_8X16		宽8像素，高16像素
  *               OLED_6X8		宽6像素，高8像素
  * @retval 无
  */
void OLED_ShowSignedNum(int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i;

	if(Number >= 0)
	{
		OLED_ShowChar(X, Y, '+', FontSize);    //显示正号
	}
	else
	{
		OLED_ShowChar(X, Y, '-', FontSize);    //显示负号
		Number = -Number;
	}
	for(i = 0; i < Length; i++)    //遍历每一位数字
	{
		OLED_ShowChar(X + (i + 1) * FontSize, Y, Number / OLED_Pow(10, Length - i - 1) % 10 + '0', FontSize);
	}
}

/**
  * @brief  OLED显示数字函数（十六进制，正整数）
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定数字左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定数字左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Number 指定要显示的数字，范围：0x00000000 ~ 0xFFFFFFFF
  * @param  Length 指定数字的长度，范围：0 ~ 8
  * @param  FontSize 指定字体大小
  *         范围：OLED_8X16		宽8像素，高16像素
  *               OLED_6X8		宽6像素，高
  * @retval 无
  */
void OLED_ShowHexNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i, SingleNumber;

	for(i = 0; i < Length; i++)    //遍历每一位数字
	{
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		if(SingleNumber < 10)    //单个数字小于10
		{
			OLED_ShowChar(X + i * FontSize, Y, SingleNumber + '0', FontSize);
		}
		else    //单个数字大于等于10
		{
			OLED_ShowChar(X + i * FontSize, Y, SingleNumber - 10 + 'A', FontSize);
		}
	}
}

/**
  * @brief  OLED显示数字函数（二进制，正整数）
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定数字左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定数字左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Number 指定要显示的数字，范围：0x00000000 ~ 0xFFFFFFFF
  * @param  Length 指定数字的长度，范围：0 ~ 16
  * @param  FontSize 指定字体大小
  *         范围：OLED_8X16		宽8像素，高16像素
  *               OLED_6X8		宽6像素，高
  * @retval 无
  */
void OLED_ShowBinNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
	uint8_t i;

	for(i = 0; i < Length; i++)    //遍历每一位数字
	{
		OLED_ShowChar(X + i * FontSize, Y, Number / OLED_Pow(2, Length - i - 1) % 2 + '0', FontSize);
	}
}

/**
  * @brief  OLED显示浮点数函数（十进制，小数）
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定数字左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定数字左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Number 指定要显示的数字，范围：-4294967295.0~4294967295.0
  * @param  IntLength 指定数字的整数位长度，范围：0 ~ 10
  * @param  FraLength 指定数字的小数位长度，范围：0 ~ 9，小数进行四舍五入显示
  * @param  FontSize 指定字体大小
  *         范围：OLED_8X16		宽8像素，高16像素
  *               OLED_6X8		宽6像素，高
  * @retval 无
  */
void OLED_ShowFloatNum(int16_t X, int16_t Y, double Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize)
{
	uint32_t PowNum, IntNum, FraNum;

	if (Number >= 0)
	{
		OLED_ShowChar(X, Y, '+', FontSize);    //显示正号
	}
	else
	{
		OLED_ShowChar(X, Y, '-', FontSize);    //显示负号
		Number = -Number;
	}

	//提取整数部分和小数部分
	IntNum = Number;                     //直接赋值给整型变量，提取整数
	Number -= IntNum;                    //将Number的整数减掉，防止之后将小数乘到整数时因数过大造成错误
	PowNum = OLED_Pow(10, FraLength);    //根据指定小数的位数，确定乘数
	FraNum = round(Number * PowNum);     //将小数乘到整数，同时四舍五入，避免显示误差
	IntNum += FraNum / PowNum;           //若四舍五入造成了进位，则需要再加给整数

	OLED_ShowNum(X + FontSize, Y, IntNum, IntLength, FontSize);                      //显示整数部分
	OLED_ShowChar(X + FontSize * (IntLength + 1), Y, '.', FontSize);                 //显示小数点
	OLED_ShowNum(X + FontSize * (IntLength + 2), Y, FraNum, FraLength, FontSize);    //显示小数部分
}

/**
  * @brief  OLED显示图像函数
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定图像左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定图像左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Width 指定图像的宽度，范围：0 ~ 128
  * @param  Height 指定图像的高度，范围：0 ~ 64
  * @param  Image 指定要显示的图像
  * @retval 无
  */
void OLED_ShowImage(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image)
{
	uint8_t i = 0, j = 0;
	int16_t Page, Shift;

	OLED_ClearArea(X, Y, Width, Height);    //将图像所在区域清空

	for(j = 0; j < (Height - 1) / 8 + 1; j++)    //遍历指定图像涉及的相关页
	{
		for(i = 0; i < Width; i++)    //遍历指定图像涉及的相关列
		{
			if(X + i >= 0 && X + i <= 127)    //超出屏幕的内容不显示
			{
				Page = Y / 8;
				Shift = Y % 8;
				if(Y < 0)    //负数坐标在计算页地址和移位时需要加一个偏移
				{
					Page -= 1;
					Shift += 8;
				}
				if(Page + j >= 0 && Page + j <= 7)    //超出屏幕的内容不显示
				{
					//显示图像在当前页的内容
					OLED_DisplayBuf[Page + j][X + i] |= Image[j * Width + i] << (Shift);
				}
				if(Page + j + 1 >= 0 && Page + j + 1 <= 7)    //超出屏幕的内容不显示
				{
					//显示图像在当前页的内容
					OLED_DisplayBuf[Page + j + 1][X + i] |= Image[j * Width + i] >> (8 - Shift);
				}
			}
		}
	}
}

/**
  * @brief  OLED使用printf函数打印格式化字符串函数（支持ASCII码和中文混合写入）
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  *         显示的中文字符需要在OLED_Data.c里的OLED_CF16x16数组定义
  *         未找到指定中文字符时，会显示默认图形（一个方框，内部一个问号）
  *         当字体大小为OLED_8X16时，中文字符以16*16点阵正常显示
  *         当字体大小为OLED_6X8时，中文字符以6*8点阵显示'?'
  * @param  X 指定格式化字符串左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定格式化字符串左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  FontSize 指定字体大小
  *         范围：OLED_8X16		宽8像素，高16像素
  *               OLED_6X8		宽6像素，高8像素
  * @param  format 指定要显示的格式化字符串，范围：ASCII码可见字符或中文字符组成的字符串
  * @param  ... 格式化字符串参数列表
  * @retval 无
  */
void OLED_Printf(int16_t X, int16_t Y, uint8_t FontSize, char *format, ...)
{
	char String[256];    //定义字符数组
	va_list arg;         //定义可变参数列表数据类型的变量arg

	va_start(arg, format);                      //从format开始，接收参数列表到arg变量
	vsprintf(String, format, arg);              //使用vsprintf打印格式化字符串和参数列表到字符数组中
	va_end(arg);                                //结束变量arg
	OLED_ShowString(X, Y, String, FontSize);    //OLED显示字符数组（字符串）
}

/**
  * @brief  OLED在指定位置画点函数
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定点的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定点的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @retval 无
  */
void OLED_DrawPoint(int16_t X, int16_t Y)
{
	if(X >= 0 && X <= 127 && Y >=0 && Y <= 63)		//超出屏幕的内容不显示
	{
		OLED_DisplayBuf[Y / 8][X] |= 0x01 << (Y % 8);
	}
}

/**
  * @brief  OLED获取指定位置点值函数
  * @param  X 指定点的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定点的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @retval 指定位置是否处于点亮状态，1点亮，0熄灭
  */
uint8_t OLED_GetPoint(int16_t X, int16_t Y)
{
	if(X >= 0 && X <= 127 && Y >=0 && Y <= 63)		//超出屏幕的内容不读取
	{
		if(OLED_DisplayBuf[Y / 8][X] & 0x01 <<(Y % 8))
		{
			return 1;
		}
	}
	return 0;
}

/**
  * @brief  OLED画线函数，使用Bresenham算法画直线，可以避免耗时的浮点运算，效率更高
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X0 指定一个端点的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y0 指定一个端点的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  X1 指定另一个端点的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y1 指定另一个端点的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @retval 无
  */
void OLED_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1)
{
	int16_t x, y, dx, dy, d, incrE, incrNE, temp;
	int16_t x0 = X0, y0 = Y0, x1 = X1, y1 = Y1;
    uint8_t yflag = 0, xyflag = 0;

	if(y0 == y1)    //横线单独处理
	{
		//0点X坐标大于1号点X坐标，则交换两点的X坐标
		if(x0 > x1)  {temp = x0; x0 = x1; x1 = temp;}
		for(x = x0; x <= x1; x++)
		{
			OLED_DrawPoint(x, y0);
		}
	}
	else if(x0 == x1)    //竖线单独处理
	{
		//0点Y坐标大于1号点Y坐标，则交换两点的Y坐标
		if(y0 > y1)  {temp = y0; y0 = y1; y1 = temp;}
		for(y = y0; y <= y1; y++)
		{
			OLED_DrawPoint(x0, y);
		}
	}
	else    //斜线处理，采用Bresenham画直线算法
	{
		//0点X坐标大于1号点X坐标，则交换两点坐标，不影响画线，但画线方向限定为第一、四象限
		if(x0 > x1)
		{
			temp = x0; x0 = x1; x1 = temp;
			temp = y0; y0 = y1; y1 = temp;
		}

		//0点Y坐标大于1号点Y坐标，则Y坐标取负，影响画线，但画线方向限定为第一象限
		if(y0 > y1)
		{
			y0 = -y0;
			y1 = -y1;
			yflag = 1;    //置yflag标志位，记住当前变换，在后续实际画线时，再将坐标换回来
		}

		//画线斜率大于1，则交换X坐标和Y坐标，影响画线，但画线方向限定为第一象限0 ~ 45度范围
		if(y1 - y0 > x1 - x0)
		{
			temp = x0; x0 = y0; y0 = temp;
			temp = x1; x1 = y1; y1 = temp;
			xyflag = 1;    //置xyflag标志位，记住当前变换，在后续实际画线时，再将坐标换回来
		}

		//Bresenham画直线算法，算法要求画线方向必须为第一象限0 ~ 45度范围
		dx = x1 - x0;
		dy = y1 - y0;
		incrE = 2 * dy;
        incrNE = 2 * (dy - dx);
        d = 2 * dy - dx;
        x = x0; y = y0;

		//画起始点，同时判断标志位，将坐标换回来
		if(yflag && xyflag)  {OLED_DrawPoint(y, -x);}
		else if(yflag)       {OLED_DrawPoint(x, -y);}
		else if(xyflag)      {OLED_DrawPoint(y, x);}
		else                 {OLED_DrawPoint(x, y);}

		while(x < x1)    //遍历x轴每个点
		{
			x++;
			if(d < 0)    //下一个点在当前点东方
			{
				d += incrE;
			}
			else    //下一个点在当前点东北方
			{
				y++;
				d += incrNE;
			}

			//每画一个点，同时判断标志位，将坐标换回来
			if(yflag && xyflag)  {OLED_DrawPoint(y, -x);}
			else if(yflag)       {OLED_DrawPoint(x, -y);}
			else if(xyflag)      {OLED_DrawPoint(y, x);}
			else                 {OLED_DrawPoint(x, y);}
		}
	}
}

/**
  * @brief  OLED画矩形函数
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定矩形左上角的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定矩形左上角的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Width 指定矩形的宽度，范围：0 ~ 128
  * @param  Height 指定矩形的高度，范围：0 ~ 64
  * @param  IsFilled 指定矩形是否填充
  *         范围：OLED_UNFILLED		不填充
  *               OLED_FILLED		填充
  * @retval 无
  */
void OLED_DrawRectangle(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled)
{
	int16_t i, j;

	OLED_DrawLine(X, Y, X + Width - 1, Y);
	OLED_DrawLine(X, Y + Height - 1, X + Width - 1, Y + Height - 1);
	OLED_DrawLine(X, Y, X, Y + Height - 1);
	OLED_DrawLine(X + Width - 1, Y, X + Width - 1, Y + Height - 1);

	if(IsFilled)    //指定三角形填充//指定矩形填充
	{
		for(i = X + 1; i < X + Width - 1; i++)
		{
			for (j = Y + 1; j < Y + Height - 1; j++)
			{
				OLED_DrawPoint(i, j);
			}
		}
	}
}

/**
  * @brief  OLED画三角形函数
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X0 指定第一个端点的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y0 指定第一个端点的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  X1 指定第二个端点的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y1 指定第二个端点的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  X2 指定第三个端点的横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y2 指定第三个端点的纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  IsFilled 指定矩形是否填充
  *         范围：OLED_UNFILLED		不填充
  *               OLED_FILLED		填充
  * @retval 无
  */
void OLED_DrawTriangle(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled)
{
	int16_t minx = X0, miny = Y0, maxx = X0, maxy = Y0;
	int16_t i, j;
	int16_t vx[] = {X0, X1, X2};
	int16_t vy[] = {Y0, Y1, Y2};

	OLED_DrawLine(X0, Y0, X1, Y1);
	OLED_DrawLine(X0, Y0, X2, Y2);
	OLED_DrawLine(X1, Y1, X2, Y2);

	if(IsFilled)    //指定三角形填充
	{
		//找到三个点中最小的X、Y坐标
		if(X1 < minx)  {minx = X1;}
		if(X2 < minx)  {minx = X2;}
		if(Y1 < miny)  {miny = Y1;}
		if(Y2 < miny)  {miny = Y2;}

		//找到三个点中最大的X、Y坐标
		if(X1 > maxx)  {maxx = X1;}
		if(X2 > maxx)  {maxx = X2;}
		if(Y1 > maxy)  {maxy = Y1;}
		if(Y2 > maxy)  {maxy = Y2;}

		//最小最大坐标之间的矩形为可能需要填充的区域，遍历此区域中所有的点
		for(i = minx; i <= maxx; i++)
		{
			for(j = miny; j <= maxy; j++)
			{
				//如果点在三角形内部，则画点，如果不在，则不做处理
				if(OLED_pnpoly(3, vx, vy, i, j))  {OLED_DrawPoint(i, j);}
			}
		}
	}
}

/**
  * @brief  OLED画圆函数，使用Bresenham算法画圆，可以避免部分耗时的浮点运算，效率更高
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定圆的圆心横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定圆的圆心纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Radius 指定圆的半径，范围：0 ~ 255
  * @param  IsFilled 指定圆是否填充
  *         范围：OLED_UNFILLED		不填充
  *               OLED_FILLED		填充
  * @retval 无
  */
void OLED_DrawCircle(int16_t X, int16_t Y, uint8_t Radius, uint8_t IsFilled)
{
	int16_t x, y, d;

	//Bresenham画圆算法，只画第一象限45 ~ 90度范围，其余部分对称
	d = 1 - Radius;
	x = 0;
	y = Radius;

	//画每个八分之一圆弧的起始点
	OLED_DrawPoint(X + x, Y + y);
	OLED_DrawPoint(X + x, Y - y);
	OLED_DrawPoint(X + y, Y + x);
	OLED_DrawPoint(X - y, Y + x);

	if(IsFilled)    //指定圆填充
	{
		OLED_DrawLine(X + x, Y - y + 1, X + x, Y + y - 1);
	}

	while(x < y)    //遍历区域内每个x坐标
	{
		x++;
		if(d < 0)    //下一个点在当前点东方
		{
			d += 2 * x + 1;
		}
		else    //下一个点在当前点东南方
		{
			y--;
			d += 2 * (x - y) + 1;
		}

		//画每个八分之一圆弧的点
		OLED_DrawPoint(X + x, Y + y);
		OLED_DrawPoint(X + x, Y - y);
		OLED_DrawPoint(X - x, Y + y);
		OLED_DrawPoint(X - x, Y - y);
		OLED_DrawPoint(X + y, Y + x);
		OLED_DrawPoint(X + y, Y - x);
		OLED_DrawPoint(X - y, Y + x);
		OLED_DrawPoint(X - y, Y - x);

		if(IsFilled)    //指定圆填充
		{
			//画中间部分
			OLED_DrawLine(X + x, Y - y + 1, X + x, Y + y - 1);
			OLED_DrawLine(X - x, Y - y + 1, X - x, Y + y - 1);

			//画两边部分
			OLED_DrawLine(X + y, Y - x + 1, X + y, Y + x - 1);
			OLED_DrawLine(X - y, Y - x + 1, X - y, Y + x - 1);
		}
	}
}

/**
  * @brief  OLED画椭圆函数，使用Bresenham算法画椭圆，可以避免部分耗时的浮点运算，效率更高
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定椭圆的圆心横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定椭圆的圆心纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  A 指定椭圆的横向半轴长度，范围：0 ~ 255
  * @param  B 指定椭圆的纵向半轴长度，范围：0 ~ 255
  * @param  IsFilled 指定圆是否填充
  *         范围：OLED_UNFILLED		不填充
  *               OLED_FILLED		填充
  * @retval 无
  */
void OLED_DrawEllipse(int16_t X, int16_t Y, uint8_t A, uint8_t B, uint8_t IsFilled)
{
	int16_t x, y;
	int16_t a = A, b = B;
	float d1, d2;

	//Bresenham画椭圆算法
	d1 = b * b + a * a * (-b + 0.5);
	x = 0;
	y = b;

	//画椭圆弧起始点
	OLED_DrawPoint(X + x, Y + y);
	OLED_DrawPoint(X + x, Y - y);

	if(IsFilled)    //指定椭圆填充
	{
		OLED_DrawLine(X + x, Y - y + 1, X + x, Y + y - 1);
	}

	//画椭圆中间部分
	while(b * b * (x + 1) < a * a * (y - 0.5))
	{
		if(d1 < 0)    //下一个点在当前点东方
		{
			d1 += b * b * (2 * x + 3);
		}
		else    //下一个点在当前点东南方
		{
			d1 += b * b * (2 * x + 3) + a * a * (-2 * y + 2);
			y --;
		}
		x++;

		//画椭圆中间部分圆弧
		OLED_DrawPoint(X + x, Y + y);
		OLED_DrawPoint(X + x, Y - y);
		OLED_DrawPoint(X - x, Y + y);
		OLED_DrawPoint(X - x, Y - y);

		if(IsFilled)    //指定椭圆填充
		{
			//画中间部分
			OLED_DrawLine(X + x, Y - y + 1, X + x, Y + y - 1);
			OLED_DrawLine(X - x, Y - y + 1, X - x, Y + y - 1);
		}
	}

	//画椭圆两侧部分
	d2 = b * b * (x + 0.5) * (x + 0.5) + a * a * (y - 1) * (y - 1) - a * a * b * b;
	while(y > 0)
	{
		if(d2 < 0)    //下一个点在当前点东方
		{
			d2 += b * b * (2 * x + 2) + a * a * (-2 * y + 3);
			x++;
		}
		else    //下一个点在当前点东南方
		{
			d2 += a * a * (-2 * y + 3);
		}
		y--;

		//画椭圆两侧部分圆弧
		OLED_DrawPoint(X + x, Y + y);
		OLED_DrawPoint(X + x, Y - y);
		OLED_DrawPoint(X - x, Y + y);
		OLED_DrawPoint(X - x, Y - y);

		if(IsFilled)    //指定椭圆填充
		{
			//画两侧部分
			OLED_DrawLine(X + x, Y - y + 1, X + x, Y + y - 1);
			OLED_DrawLine(X - x, Y - y + 1, X - x, Y + y - 1);
		}
	}
}

/**
  * @brief  OLED画圆弧函数，使用Bresenham画圆算法
  *         调用此函数后，要想真正地呈现在屏幕上，还需调用更新函数
  * @param  X 指定圆弧的圆心横坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 127
  * @param  Y 指定圆弧的圆心纵坐标，范围：-32768 ~ 32767，屏幕区域：0 ~ 63
  * @param  Radius 指定圆弧的半径，范围：0 ~ 255
  * @param  StartAngle 指定圆弧的起始角度，范围：-180 ~ 180
  *         水平向右为0度，水平向左为180度或-180度，下方为正数，上方为负数，顺时针旋转
  * @param  EndAngle 指定圆弧的终止角度，范围：-180 ~ 180
  *         水平向右为0度，水平向左为180度或-180度，下方为正数，上方为负数，顺时针旋转
  * @param  IsFilled 指定圆是否填充
  *         范围：OLED_UNFILLED		不填充
  *               OLED_FILLED		填充
  * @retval 无
  */
void OLED_DrawArc(int16_t X, int16_t Y, uint8_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled)
{
	int16_t x, y, d, j;

	//此函数借用Bresenham画圆算法
	d = 1 - Radius;
	x = 0;
	y = Radius;

	//在画圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理
	if(OLED_IsInAngle(x, y, StartAngle, EndAngle))   {OLED_DrawPoint(X + x, Y + y);}
	if(OLED_IsInAngle(x, -y, StartAngle, EndAngle))  {OLED_DrawPoint(X + x, Y - y);}
	if(OLED_IsInAngle(y, x, StartAngle, EndAngle))   {OLED_DrawPoint(X + y, Y + x);}
	if(OLED_IsInAngle(-y, x, StartAngle, EndAngle))  {OLED_DrawPoint(X - y, Y + x);}

	if(IsFilled)    //指定圆弧填充
	{
		for(j = -y + 1; j < y; j++)
		{
			//在填充圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理
			if(OLED_IsInAngle(x, j, StartAngle, EndAngle))   {OLED_DrawPoint(X + x, Y + j);}
		}
	}

	while(x < y)    //遍历区域内每个x坐标
	{
		x++;
		if(d < 0)    //下一个点在当前点东方
		{
			d += 2 * x + 1;
		}
		else    //下一个点在当前点东南方
		{
			y--;
			d += 2 * (x - y) + 1;
		}

		//在画圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理
		if(OLED_IsInAngle(x, y, StartAngle, EndAngle))    {OLED_DrawPoint(X + x, Y + y);}
		if(OLED_IsInAngle(x, -y, StartAngle, EndAngle))   {OLED_DrawPoint(X + x, Y - y);}
		if(OLED_IsInAngle(-x, y, StartAngle, EndAngle))   {OLED_DrawPoint(X - x, Y + y);}
		if(OLED_IsInAngle(-x, -y, StartAngle, EndAngle))  {OLED_DrawPoint(X - x, Y - y);}
		if(OLED_IsInAngle(y, x, StartAngle, EndAngle))    {OLED_DrawPoint(X + y, Y + x);}
		if(OLED_IsInAngle(y, -x, StartAngle, EndAngle))   {OLED_DrawPoint(X + y, Y - x);}
		if(OLED_IsInAngle(-y, x, StartAngle, EndAngle))   {OLED_DrawPoint(X - y, Y + x);}
		if(OLED_IsInAngle(-y, -x, StartAngle, EndAngle))  {OLED_DrawPoint(X - y, Y - x);}

		if(IsFilled)    //指定圆弧填充
		{
			//画中间部分
			for(j = -y + 1; j < y; j++)
			{
				//在填充圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理
				if(OLED_IsInAngle(x, j, StartAngle, EndAngle))   {OLED_DrawPoint(X + x, Y + j);}
				if(OLED_IsInAngle(-x, j, StartAngle, EndAngle))  {OLED_DrawPoint(X - x, Y + j);}
			}

			//画两边部分
			for(j = -x + 1; j < x; j++)
			{
				//在填充圆的每个点时，判断指定点是否在指定角度内，在，则画点，不在，则不做处理
				if(OLED_IsInAngle(y, j, StartAngle, EndAngle))   {OLED_DrawPoint(X + y, Y + j);}
				if(OLED_IsInAngle(-y, j, StartAngle, EndAngle))  {OLED_DrawPoint(X - y, Y + j);}
			}
		}
	}
}

/*********************功能函数*/
