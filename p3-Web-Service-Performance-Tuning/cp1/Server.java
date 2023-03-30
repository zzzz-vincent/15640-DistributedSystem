/* Sample code for basic Server */

public class Server {
	// VM_ID of the first created VM, acts as primary node to provision more VMs
	private static final int PRIMARY_VM_ID = 1;

	public static void log(String s) {
		System.out.println("MyServer says: " + s);
	}

	public static void main ( String args[] ) throws Exception {
		if (args.length != 3) throw new Exception("Need 3 args: <cloud_ip> <cloud_port> <VM id>");

		int vmId = Integer.parseInt(args[2]);

		ServerLib SL = new ServerLib( args[0], Integer.parseInt(args[1]));
		new Thread(() -> runSvr(SL)).start();

		SL.register_frontend();
//		log(String.format("t: %f", SL.getTime()));

		if (vmId == PRIMARY_VM_ID) {
			int time = (int) Math.floor(SL.getTime());
			switch (time) {
				case 1: // 3500
				case 2: // 5000
				case 3: // 5000
				case 4: // 5000
				case 5: // 4100
					startSvr(SL, 1);
					break;
				case 0: // 2000
				case 6: // 3333
				case 9:  // 1250
				case 10: // 1250
				case 15: // 1250
				case 16: // 1500
				case 17: // 1250
				case 23: // 1250
					startSvr(SL, 2);
					break;
				case 7:  // 2700 - 1000
				case 11: // 1250 - 1000
					startSvr(SL, 3);
					break;
				case 8:  // 1000
				case 12: // 1000
				case 14: // 1000
				case 13: // 909
				case 18: // 1000
				case 19: // 833
				case 20: // 666
				case 21: // 714
				case 22: // 1000
					startSvr(SL, 4);
					break;
			}
		}
//		runSvr(SL);
	}

	public static void startSvr(ServerLib SL, int num) {
		for (int i = 0; i < num ; i++) {
			new Thread(SL::startVM).start();
		}
	}

	public static void runSvr(ServerLib SL) {
		// main loop
		while (true) {
			Cloud.FrontEndOps.Request r = SL.getNextRequest();
//			log(String.format("rid: %s", r));
			SL.processRequest( r );
		}
	}


}

