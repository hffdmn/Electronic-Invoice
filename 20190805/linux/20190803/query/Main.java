import java.io.*;
import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.ptr.PointerByReference;
import com.sun.jna.ptr.IntByReference;

public class Main{

	public interface Clibrary extends Library {

		Clibrary INSTANCE = (Clibrary)Native.loadLibrary("secquery", Clibrary.class);

		public int SEC_storage_api_init(PointerByReference pbr, String str);
		public int SEC_storage_api_free(Pointer p);
        public int SEC_storage_api_json_free(Pointer p, int dataNum);
        public int SEC_storage_api_buff_free(Pointer p);
		public int SEC_storage_api_invoiceId_query(Pointer p, String invoiceId, PointerByReference pbr, 
											IntByReference ibr, String globalID, String parentID, PointerByReference reIDPbr); 
		public int SEC_storage_api_elem_query(Pointer p, String queryJson, PointerByReference pbr, 
											IntByReference ibr, String globalID, String parentID, PointerByReference reIDPbr); 
		public int SEC_storage_api_normal_query(Pointer p, String queryJson, int offset, int num, 
											PointerByReference pbr, IntByReference ibr, String globalID, String parentID, PointerByReference reIDPbr); 
		public int SEC_storage_api_pdf_download(Pointer p, String invoiceId, PointerByReference pbr, 
											IntByReference ibr, String globalID, String parentID, PointerByReference reIDPbr); 
		public int SEC_storage_api_img_download(Pointer p, String invoiceId, PointerByReference pbr, 
											IntByReference ibr, String globalID, String parentID, PointerByReference reIDPbr); 
		public int SEC_storage_api_ofd_download(Pointer p, String invoiceId, PointerByReference pbr, 
											IntByReference ibr, String globalID, String parentID, PointerByReference reIDPbr); 
	}

	public static void main(String[] args) throws Exception{
		
		PointerByReference pbr = new PointerByReference();
		Clibrary.INSTANCE.SEC_storage_api_init(pbr, "./gateway.ini");
		Pointer p = pbr.getValue();

        String invoiceID = "1136097387786768384";
		String globalID = "1111111111111";
		String parentID = "123456";
	
		int random;
		int i;
		int j;
		long st = System.currentTimeMillis();
		IntByReference ibr = new IntByReference();
		PointerByReference pbr1 = new PointerByReference();
		PointerByReference reIDPbr = new PointerByReference();
		for(i = 0; i < 1; i++) {
			//String invoiceID = "" + (int)(1 + Math.random() * 1000);
			//System.out.println(invoiceID);	
			
			Clibrary.INSTANCE.SEC_storage_api_invoiceId_query(p, invoiceID, pbr1, ibr, globalID, parentID, reIDPbr);
			//Clibrary.INSTANCE.SEC_storage_api_invoiceId_query(p, invoiceID, pbr1, ibr, null, null, null);
			//Clibrary.INSTANCE.SEC_storage_api_elem_query(p, "{\"invoiceCode\":\"XXXXXXXXXXXX\",\"invoiceNum\":\"07666350\"}", pbr1, ibr, globalID, parentID, reIDPbr);
			//Clibrary.INSTANCE.SEC_storage_api_normal_query(p, "{\"accStatus\":\"X\", \"revStatus\":\"Y\"}", 0, 100,  pbr1, ibr, globalID, parentID, reIDPbr);
			//Clibrary.INSTANCE.SEC_storage_api_normal_query(p, "{\"sellerName\":\"XX\"}", 0, 100,  pbr1, ibr, globalID, parentID, reIDPbr);
			//Clibrary.INSTANCE.SEC_storage_api_normal_query(p, "{\"inIssuType\":\"1\",\"lastTime\":\"20180622155555\",\"lastID\":\"1\"}", 12, 4,  pbr1, ibr, globalID, parentID, reIDPbr);
			//Clibrary.INSTANCE.SEC_storage_api_normal_query(p, "{\"inIssuType\":\"1\"}", 0, 4,  pbr1, ibr, globalID, parentID, reIDPbr);
		
			//	Pointer reIDP = reIDPbr.getValue();
			//	String reID = reIDP.getString(0);
			//	System.out.println("reID is " + reID);

				Pointer pVals = pbr1.getValue();
				int numVals = ibr.getValue();
				final String[]  strArray = pVals.getStringArray(0, numVals);
				System.out.println("retrieved " + numVals + " values :");
				for(j=0; j < numVals; j++) {
					System.out.println(i + "-->" + strArray[j]);
				}
				Clibrary.INSTANCE.SEC_storage_api_json_free(pVals, numVals);
			}

		//	Clibrary.INSTANCE.SEC_storage_api_pdf_download(p, invoiceID, pbr1, ibr, globalID, parentID, reIDPbr);
			//Clibrary.INSTANCE.SEC_storage_api_img_download(p, invoiceID, pbr1, ibr, globalID, parentID, reIDPbr);
			//Clibrary.INSTANCE.SEC_storage_api_ofd_download(p, invoiceID, pbr1, ibr, globalID, parentID, reIDPbr);
			//System.out.println("buff len is " + ibr.getValue());
			//byte[] binData = pbr1.getValue().getByteArray(0, ibr.getValue());
			//OutputStream out = new FileOutputStream("22.pdf");
			//InputStream is = new ByteArrayInputStream(binData);
			//byte[] buff = new byte[1024];
			//int len = 0;
			//while((len = is.read(buff)) != -1) {
			//	out.write(buff, 0, len);
			//}
			//is.close();
			//out.close();
			//Clibrary.INSTANCE.SEC_storage_api_buff_free(pbr1.getValue());
		//}
        Thread.sleep(10000);
		Clibrary.INSTANCE.SEC_storage_api_free(p);
	}	
}
