import type {Sql} from 'postgres';
import type {Database} from './repository.ts';

/** Fronteira do driver; valores sempre parametrizados, resultado limitado por SELECTs explícitos. */
export function postgresDatabase(sql: Sql): Database {
  const db: Database={
    query:async<T>(text: string,params: unknown[]=[])=>await sql.unsafe(text,params as never[]) as unknown as T[],
    transaction:async<T>(work: (tx: Database)=>Promise<T>): Promise<T>=>{
      const result=await sql.begin(async tx=>{
        const transaction: Database={
          query:async<R>(text: string,params: unknown[]=[])=>await tx.unsafe(text,params as never[]) as unknown as R[],
          transaction:async()=>{throw new Error('nested_transaction_not_supported');},
        };
        return work(transaction);
      });
      return result as T;
    },
  };
  return db;
}
