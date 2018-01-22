#include "mbed.h"
#include "stm32f413h_discovery.h"
#include "stm32f413h_discovery_ts.h"
#include "stm32f413h_discovery_lcd.h"
#include "FATFileSystem.h"
#include "F413ZH_SD_BlockDevice.h"
#include "tensor.hpp"
#include "image.h"
#include "deep_mnist_mlp.hpp"
#include "models/deep_mlp.hpp"

Serial pc(USBTX, USBRX, 115200);
F413ZH_SD_BlockDevice bd;
FATFileSystem fs("fs");

InterruptIn button(USER_BUTTON);

TS_StateTypeDef  TS_State = {0};

volatile bool trigger_inference = false;

void trigger_inference_cb(void){ trigger_inference = true; }

template<typename T>
void clear(Image<T>& img){
    for(int i = 0; i < img.get_xDim(); i++){
        for(int j = 0; j < img.get_yDim(); j++){
            img(i,j) = 0;
        }   
    }
}

template<typename T>
void printImage(const Image<T>& img){

    //pc.printf("\n\n{\n");
    for(int i = 0; i < img.get_xDim(); i++){
        //pc.printf("[ ");
        for(int j = 0; j < img.get_yDim(); j++){
            pc.printf("%f, ", img(i,j));
        }
        pc.printf("]\n\r");
    }
    //pc.printf("}\n");

}
template<typename T>
Image<T> box_blur(const Image<T>& img){
    Image<T> tmp(img.get_xDim(), img.get_yDim());
    clear(tmp);
    for(int i = 4; i < img.get_xDim() - 4; i++){
        for(int j = 4; j < img.get_yDim() - 4; j++){
            //for(int ii = -1; ii < 2; ii++){
            //    for(int jj = -1; jj < 2; jj++){
            //        tmp(i,j) += img(i+ii, j+jj);
            //    }
            //}
            tmp(i,j) = img(i-1, j-1) + img(i, j-1) + img(i+1, j-1) +
                       img(i-1, j) + 3.0*img(i, j) + img(i+1, j) + 
                       img(i-1, j+1) + img(i, j+1) + img(i+1, j+1);
            tmp(i,j) /= 11.0;
            //tmp(i,j) /= 9.0;
        }
    }

    return tmp;
}


int main()
{
    uint16_t x1, y1;
    pc.printf("program start...\r\n");

    ON_ERR(bd.init(), "SDBlockDevice init ");
    ON_ERR(fs.mount(&bd), "Mounting the filesystem on \"/fs\". ");
    Image<float>* img = new Image<float>(240, 240);

   // int prediction = runMLP("/fs/testData/deep_mlp/import-Placeholder_0.idx");
   // pc.printf("prediction: %d\r\n\r\n\r\n\r\n", prediction);

    BSP_LCD_Init();
    button.rise(&trigger_inference_cb);

    /* Touchscreen initialization */
    if (BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize()) == TS_ERROR) {
        printf("BSP_TS_Init error\n");
    }

    /* Clear the LCD */
    BSP_LCD_Clear(LCD_COLOR_WHITE);

    /* Set Touchscreen Demo1 description */
    //BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
    //BSP_LCD_FillRect(0, 0, BSP_LCD_GetXSize(), 40);
    //BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
    //BSP_LCD_SetBackColor(LCD_COLOR_GREEN);
    //BSP_LCD_SetFont(&Font16);
    //BSP_LCD_DisplayStringAt(0, 15, (uint8_t *)"Touch the screen", CENTER_MODE);

    Context ctx;
    clear(*img);


    while (1) {
        BSP_TS_GetState(&TS_State);
        if(trigger_inference){
            
            Image<float> smallImage = resize(*img, 64, 64);

            //printImage(smallImage);
            //printImage(*img);

            Image<float> chopped = chop(smallImage);
            pc.printf("Done chopping\n\n");
            Image<float> img20   = resize(chopped, 20, 20);
            pc.printf("Done resizing\n\n");
            Image<float> img28   = pad(img20, 4, 4);
            //Image<float> img28   = pad(img20, 0, 0);
            //printImage(img28);
            pc.printf("Done padding\n\n");
            smallImage.~Image<float>();
            chopped.~Image<float>();
            img20.~Image<float>();
            delete img;

            //smallImage = pad(resize(chop(smallImage), 20, 20), 4, 4);
            Image<float> img28_2 = box_blur(img28);
            pc.printf("Done blurring\n\r");
            img28.~Image<float>();

            //printImage(img28_2);
            pc.printf("Reshaping\n\r");
            img28_2.get_data()->resize({1, 784});
            pc.printf("Creating Graph\n\r");
            get_deep_mlp_ctx(ctx, img28_2.get_data());
            pc.printf("Evaluating\n\r");
            ctx.eval();
            S_TENSOR prediction = ctx.get({"y_pred:0"});
            pc.printf("Number guessed %d\n\r", *(prediction->read<int>(0,0)));
            trigger_inference = false;
            img = new Image<float>(240, 240);
            clear(*img);
            break;
        }
        if(TS_State.touchDetected) {
            /* One or dual touch have been detected          */

            /* Get X and Y position of the first touch post calibrated */
            x1 = TS_State.touchX[0];
            y1 = TS_State.touchY[0];
            //printf("Touch Detected x=%d y=%d\n", x1, y1);
            //(*img)((int) x1, (int) y1) = 255;
            img->draw_circle(y1, x1, 7); //Screen not in image x,y format. Must transpose

            BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
            BSP_LCD_FillCircle(x1, y1, 5);

            wait_ms(5);
        }
    }

    ON_ERR(fs.unmount(), "fs unmount ");
    ON_ERR(bd.deinit(), "SDBlockDevice de-init ");
}
